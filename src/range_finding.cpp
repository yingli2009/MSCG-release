//
//  range_finding.cpp
//  
//
//  Copyright (c) 2016 The Voth Group at The University of Chicago. All rights reserved.
//

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "force_computation.h"
#include "geometry.h"
#include "interaction_model.h"
#include "matrix.h"
#include "range_finding.h"
#include "misc.h"

//----------------------------------------------------------------------------
// Prototypes for private implementation routines.
//----------------------------------------------------------------------------

// Initialization of storage for the range value arrays and their computation

void initialize_ranges(int tol, double* const lower_cutoffs, double* const upper_cutoffs, std::vector<unsigned> &num);

void initialize_single_class_range_finding_temps(InteractionClassSpec *iclass, InteractionClassComputer *icomp, TopologyData *topo_data);

// Helper functions that issues failure warnings and do special setup
void report_unrecognized_class_subtype(InteractionClassSpec *iclass);

// Functions for computing the full range of sampling of a given class of interaction in a given trajectory.
void calc_isotropic_two_body_sampling_range(InteractionClassComputer* const icomp, const rvec* x, const real *simulation_box_half_lengths, MATRIX_DATA* const mat);
void calc_angular_three_body_sampling_range(InteractionClassComputer* const icomp, const rvec* x, const real *simulation_box_half_lengths, MATRIX_DATA* const mat);
void calc_dihedral_four_body_interaction_sampling_range(InteractionClassComputer* const icomp, const rvec* x, const real *simulation_box_half_lengths, MATRIX_DATA* const mat);
void calc_nothing(InteractionClassComputer* const icomp, const rvec* x, const real *simulation_box_half_lengths, MATRIX_DATA* const mat);

void write_interaction_range_data_to_file(CG_MODEL_DATA* const cg, MATRIX_DATA* const mat,  FILE* const nonbonded_spline_output_filep, FILE* const bonded_spline_output_filep);

void write_iclass_range_specifications(InteractionClassComputer* const icomp, char **name, MATRIX_DATA* const mat, FILE* const solution_spline_output_file);
void write_single_range_specification(InteractionClassComputer* const icomp, char **name, MATRIX_DATA* const mat, FILE* const solution_spline_output_file, const int index_among_defined);

void read_interaction_file_and_build_matrix(MATRIX_DATA* mat, InteractionClassComputer* const icomp, double volume, TopologyData* const topo_data, char ** const name);
void read_one_param_dist_file_pair(InteractionClassComputer* const icomp, char ** const name, MATRIX_DATA* mat, const int index_among_defined_intrxns, int &counter, double num_of_pairs, double volume);
void read_one_param_dist_file_other(InteractionClassComputer* const icomp, char ** const name, MATRIX_DATA* mat, const int index_among_defined_intrxns, int &counter, double num_of_pairs);

// Output parameter distribution functions
void open_parameter_distribution_files_for_class(InteractionClassComputer* const icomp, char **name); 
void close_parameter_distribution_files_for_class(InteractionClassComputer* const icomp);
void remove_dist_files(InteractionClassComputer* const icomp, char **name);
void generate_parameter_distribution_histogram(InteractionClassComputer* const icomp, char **name);

// Dummy implementations
void do_not_initialize_fm_matrix(MATRIX_DATA* const mat);

//------------------------------------------------------------------------
//    Implementation
//------------------------------------------------------------------------

void do_not_initialize_fm_matrix(MATRIX_DATA* const mat) {}

void initialize_ranges(int tol, double* const lower_cutoffs, double* const upper_cutoffs, std::vector<unsigned> &num)
{
    for (int i = 0; i < tol; i++) {
        lower_cutoffs[i] = VERYLARGE;
        upper_cutoffs[i] = -VERYLARGE;
        num[i] = i + 1;
    }
}

void initialize_range_finding_temps(CG_MODEL_DATA* const cg)
{   
	std::list<InteractionClassSpec*>::iterator iclass_iterator;
	std::list<InteractionClassComputer*>::iterator icomp_iterator;
	for(iclass_iterator=cg->iclass_list.begin(), icomp_iterator=cg->icomp_list.begin(); (iclass_iterator != cg->iclass_list.end()) && (icomp_iterator != cg->icomp_list.end()); iclass_iterator++, icomp_iterator++) {	
		initialize_single_class_range_finding_temps((*iclass_iterator), (*icomp_iterator), &cg->topo_data);
	}
    initialize_single_class_range_finding_temps(&cg->three_body_nonbonded_interactions, &cg->three_body_nonbonded_computer, &cg->topo_data);
	cg->pair_nonbonded_cutoff2 = VERYLARGE * VERYLARGE;
}

void initialize_single_class_range_finding_temps(InteractionClassSpec *iclass, InteractionClassComputer *icomp, TopologyData *topo_data) 
{
	iclass->setup_for_defined_interactions(topo_data);
	
    icomp->ispec = iclass;
    if (iclass->class_type == kPairNonbonded) {
        icomp->calculate_fm_matrix_elements = calc_isotropic_two_body_sampling_range;
    } else if (iclass->class_type == kPairBonded) {
        icomp->calculate_fm_matrix_elements = calc_isotropic_two_body_sampling_range;
    } else if (iclass->class_type == kAngularBonded) {
        if (iclass->class_subtype == 0) { // Angle based angular interactions
			icomp->calculate_fm_matrix_elements = calc_angular_three_body_sampling_range;
        } else if (iclass->class_subtype == 1) { // Distance based angular interactions
			icomp->calculate_fm_matrix_elements = calc_isotropic_two_body_sampling_range;
		} else {
			report_unrecognized_class_subtype(iclass);
		}
    } else if (iclass->class_type == kDihedralBonded) {
        if (iclass->class_subtype == 0) { //Angle based dihedral interactions
			icomp->calculate_fm_matrix_elements = calc_dihedral_four_body_interaction_sampling_range;
        } else if (iclass->class_subtype == 1) { // Distance based dihedral interactions
			icomp->calculate_fm_matrix_elements = calc_isotropic_two_body_sampling_range;
		} else {
			report_unrecognized_class_subtype(iclass);
		}
	} else { // For three_body_interactions
    	icomp->calculate_fm_matrix_elements = calc_nothing;
    }
    
	iclass->n_cg_types = int(topo_data->n_cg_types);
    initialize_ranges(iclass->get_n_defined(), iclass->lower_cutoffs, iclass->upper_cutoffs, iclass->defined_to_matched_intrxn_index_map);
    iclass->n_to_force_match = iclass->get_n_defined();
    iclass->interaction_column_indices = std::vector<unsigned>(iclass->n_to_force_match + 1);
	
	char** name = select_name(iclass, topo_data->name);
	if(iclass->output_parameter_distribution == 1 || iclass->output_parameter_distribution == 2 ){
		if (iclass->class_type == kPairNonbonded || iclass->class_type == kPairBonded || 
		           iclass->class_type == kAngularBonded || iclass->class_type == kDihedralBonded) {
		    open_parameter_distribution_files_for_class(icomp, name);
		} else {
			// do nothing here
		}
	}
}

void report_unrecognized_class_subtype(InteractionClassSpec *iclass)
{
	printf("Unrecognized %s class subtype!\n", iclass->get_full_name().c_str());
	fflush(stdout);
	exit(EXIT_FAILURE);
}

//--------------------------------------------------------------------------

void calc_isotropic_two_body_sampling_range(InteractionClassComputer* const icomp, const rvec* x, const real *simulation_box_half_lengths, MATRIX_DATA* const mat)
{
    int particle_ids[2] = {icomp->k, icomp->l};
    double param;
    calc_distance(particle_ids, x, simulation_box_half_lengths, param);

    if (icomp->ispec->lower_cutoffs[icomp->index_among_defined_intrxns] > param) icomp->ispec->lower_cutoffs[icomp->index_among_defined_intrxns] = param;
    if (icomp->ispec->upper_cutoffs[icomp->index_among_defined_intrxns] < param) icomp->ispec->upper_cutoffs[icomp->index_among_defined_intrxns] = param;
	
	if (icomp->ispec->output_parameter_distribution == 1 || icomp->ispec->output_parameter_distribution == 2) {
		if (icomp->ispec->class_type == kPairBonded || icomp->ispec->class_type == kAngularBonded || icomp->ispec->class_type == kDihedralBonded) {
			fprintf(icomp->ispec->output_range_file_handles[icomp->index_among_defined_intrxns], "%lf\n", param);
		} else if( (icomp->ispec->class_type == kPairNonbonded) && (param < icomp->ispec->cutoff)) {
		 	fprintf(icomp->ispec->output_range_file_handles[icomp->index_among_defined_intrxns], "%lf\n", param);
		}
	}
}

void calc_angular_three_body_sampling_range(InteractionClassComputer* const icomp, const rvec* x, const real *simulation_box_half_lengths, MATRIX_DATA* const mat)
{
    int particle_ids[3] = {icomp->k, icomp->l, icomp->j}; // end indices (k, l) followed by center index (j)
    double param;
    calc_angle(particle_ids, x, simulation_box_half_lengths, param);

    if (icomp->ispec->lower_cutoffs[icomp->index_among_defined_intrxns] > param) icomp->ispec->lower_cutoffs[icomp->index_among_defined_intrxns] = param;
    if (icomp->ispec->upper_cutoffs[icomp->index_among_defined_intrxns] < param) icomp->ispec->upper_cutoffs[icomp->index_among_defined_intrxns] = param;
	
	if (icomp->ispec->output_parameter_distribution == 1 || icomp->ispec->output_parameter_distribution == 2) fprintf(icomp->ispec->output_range_file_handles[icomp->index_among_defined_intrxns], "%lf\n", param);
}

void calc_dihedral_four_body_interaction_sampling_range(InteractionClassComputer* const icomp, const rvec* x, const real *simulation_box_half_lengths, MATRIX_DATA* const mat)
{
    int particle_ids[4] = {icomp->k, icomp->l, icomp->i, icomp->j}; // end indices (k, l) followed by central bond indices (i, j)
    double param;
    calc_dihedral(particle_ids, x, simulation_box_half_lengths, param);
    if (icomp->ispec->lower_cutoffs[icomp->index_among_defined_intrxns] > param) icomp->ispec->lower_cutoffs[icomp->index_among_defined_intrxns] = param;
    if (icomp->ispec->upper_cutoffs[icomp->index_among_defined_intrxns] < param) icomp->ispec->upper_cutoffs[icomp->index_among_defined_intrxns] = param;
	
	if (icomp->ispec->output_parameter_distribution == 1 || icomp->ispec->output_parameter_distribution == 2) fprintf(icomp->ispec->output_range_file_handles[icomp->index_among_defined_intrxns], "%lf\n", param);
}

void calc_nothing(InteractionClassComputer* const icomp, const rvec* x, const real *simulation_box_half_lengths, MATRIX_DATA* const mat) {
}

//--------------------------------------------------------------------------

void write_range_files(CG_MODEL_DATA* const cg, MATRIX_DATA* const mat)
{	
    FILE* nonbonded_interaction_output_file_handle = open_file("rmin.in", "w");
    FILE* bonded_interaction_output_file_handle = open_file("rmin_b.in", "w");
    
    write_interaction_range_data_to_file(cg, mat, nonbonded_interaction_output_file_handle, bonded_interaction_output_file_handle);
    
 	fclose(nonbonded_interaction_output_file_handle);
    fclose(bonded_interaction_output_file_handle);
}

void write_interaction_range_data_to_file(CG_MODEL_DATA* const cg, MATRIX_DATA* const mat, FILE* const nonbonded_spline_output_filep, FILE* const bonded_spline_output_filep)
{   
	std::list<InteractionClassSpec*>::iterator iclass_iterator;
	std::list<InteractionClassComputer*>::iterator icomp_iterator;
    for(iclass_iterator = cg->iclass_list.begin(), icomp_iterator = cg->icomp_list.begin(); (iclass_iterator != cg->iclass_list.end()) && (icomp_iterator != cg->icomp_list.end()); iclass_iterator++, icomp_iterator++) {
        char** name = select_name((*iclass_iterator), cg->name);
        if ((*iclass_iterator)->class_type == kPairNonbonded) {
            write_iclass_range_specifications(*icomp_iterator, name, mat, nonbonded_spline_output_filep);
        } else {
            write_iclass_range_specifications(*icomp_iterator, name, mat, bonded_spline_output_filep);
        }
    }
}

void write_iclass_range_specifications(InteractionClassComputer* const icomp, char **name, MATRIX_DATA* const mat, FILE* const solution_spline_output_file) 
{
	// Name is selected in calling function write_interaction_range_data_to_file.
    InteractionClassSpec *iclass = icomp->ispec;
    for (int i = 0; i < iclass->get_n_defined(); i++) {
        int index_among_matched_interactions = iclass->defined_to_matched_intrxn_index_map[i];
        if (index_among_matched_interactions > 0) {
            write_single_range_specification(icomp, name, mat, solution_spline_output_file, i);
        }
    }
	
	if (iclass->output_parameter_distribution == 1 || iclass->output_parameter_distribution == 2) {
		if (iclass->class_type == kPairNonbonded || iclass->class_type == kPairBonded || 
		           iclass->class_type == kAngularBonded || iclass->class_type == kDihedralBonded) {
			close_parameter_distribution_files_for_class(icomp);
			generate_parameter_distribution_histogram(icomp, name);
			remove_dist_files(icomp, name);
		} else {
			// do nothing for these
		}
	}
}

void write_single_range_specification(InteractionClassComputer* const icomp, char **name, MATRIX_DATA* const mat, FILE* const solution_spline_output_file, const int index_among_defined)
{
	// Name is selected in 2x up calling function write_single_range_specification
    InteractionClassSpec* ispec = icomp->ispec;
    std::string basename = ispec->get_interaction_name(name, index_among_defined, " ");
	
	fprintf(solution_spline_output_file, "%s ", basename.c_str());

    if (fabs(ispec->upper_cutoffs[index_among_defined] + VERYLARGE) < VERYSMALL_F) {
        ispec->upper_cutoffs[index_among_defined] = -1.0;
        ispec->lower_cutoffs[index_among_defined] = -1.0;
    } else if (ispec->class_type == kPairNonbonded) {
        if (ispec->lower_cutoffs[index_among_defined] > ispec->cutoff) {
            ispec->upper_cutoffs[index_among_defined] = -1.0;
            ispec->lower_cutoffs[index_among_defined] = -1.0;
        } else if (ispec->upper_cutoffs[index_among_defined] > ispec->cutoff) {
            ispec->upper_cutoffs[index_among_defined] = ispec->cutoff;
        }
    }
    
    fprintf(solution_spline_output_file, "%lf %lf", ispec->lower_cutoffs[index_among_defined], ispec->upper_cutoffs[index_among_defined]);
    if (ispec->upper_cutoffs[index_among_defined] == -1.0) { // there is no sampling here.
		fprintf(solution_spline_output_file, " none");	    
    } else {
	    fprintf(solution_spline_output_file, " fm");
	}
	
	fprintf(solution_spline_output_file, "\n");
}

void open_parameter_distribution_files_for_class(InteractionClassComputer* const icomp, char **name) 
{
	printf("Generating parameter distribution histogram for %s interactions.\n", icomp->ispec->get_full_name().c_str());
	// The correct name is selected in calling function initialize_single_class_range_finding_temps
    InteractionClassSpec* ispec = icomp->ispec;
	std::string filename;
	ispec->output_range_file_handles = new FILE*[ispec->get_n_defined()];
	
	for (int i = 0; i < ispec->get_n_defined(); i++) {
	 	filename = ispec->get_basename(name, i,  "_") + ".dist";
  		ispec->output_range_file_handles[i] = open_file(filename.c_str(), "w");
	}		
}

void close_parameter_distribution_files_for_class(InteractionClassComputer* const icomp) 
{
    InteractionClassSpec* ispec = icomp->ispec;	
	for (int i = 0; i < ispec->get_n_defined(); i++) {
		fclose(ispec->output_range_file_handles[i]);
	}
	delete [] ispec->output_range_file_handles;
}

void remove_dist_files(InteractionClassComputer* const icomp, char **name) 
{
	// Name is selected in calling function 2x up named write_interaction_range_data_to_file.
    InteractionClassSpec* ispec = icomp->ispec;	
    if(ispec->output_parameter_distribution != 1) return;
    for (int i = 0; i < ispec->get_n_defined(); i++) {
		// get name of dist file
		std::string filename = ispec->get_basename(name, i, "_") + ".dist";
		// remove file
		remove(filename.c_str());
	}
}

void generate_parameter_distribution_histogram(InteractionClassComputer* const icomp, char **name)
{
	// Name is selected in calling function 2x up named write_interaction_range_data_to_file.
	InteractionClassSpec* ispec = icomp->ispec;	
	
	std::string filename;
	std::ifstream dist_stream;
	std::ofstream hist_stream;
	int num_bins = 0;
	int	curr_bin;
	double value; 
	double* bin_centers;
	unsigned long* bin_counts;
	for (int i = 0; i < ispec->get_n_defined(); i++) {
		
		// Set-up histogram based on interaction binwidth
		if (ispec->upper_cutoffs[i] == -1.0) { // there is no sampling here. default allocate
		  num_bins = 1;
		} else {	
	     ispec->adjust_cutoffs_for_basis(i);
		 num_bins = ( 2 * (int)( (ispec->upper_cutoffs[i] - ispec->lower_cutoffs[i]) / ispec->get_fm_binwidth() + 0.5 ));
		}
		bin_centers = new double[num_bins]();
        bin_counts = new unsigned long[num_bins]();
        
        bin_centers[0] = ispec->lower_cutoffs[i] + 0.25 * ispec->get_fm_binwidth();
        for (int j = 1; j < num_bins; j++) {
	      bin_centers[j] = bin_centers[j - 1] + (0.5 * ispec->get_fm_binwidth());
        }
		
		// Open distribution file
	 	filename = ispec->get_basename(name, i, "_") + ".dist";
		check_and_open_in_stream(dist_stream, filename.c_str()); 
		
		// Populate histogram by reading distribution file
		dist_stream >> value;
		while (!dist_stream.fail()) {
		  curr_bin = (int)(floor((value - ispec->lower_cutoffs[i] + VERYSMALL_F) / (0.5 * ispec->get_fm_binwidth())));	
			if( (curr_bin < num_bins) && (curr_bin >= 0) ) {
				bin_counts[curr_bin]++;
			} else if (curr_bin > num_bins) {
				printf("Warning: Bin %d is out-of-bounds. Array size: %d\n", curr_bin, num_bins);
				fflush(stdout);
			}
			dist_stream >> value;
		}

		// Write histogram to file
		filename = ispec->get_basename(name, i, "_") + ".hist";
		hist_stream.open(filename, std::ofstream::out);
		hist_stream << "#center\tcounts\n";
		for (int j = 0; j < num_bins; j++) {
			hist_stream << bin_centers[j] << "\t" << bin_counts[j] << "\n";
		}
		
		// Close files
		hist_stream.close();
		dist_stream.close();
		delete [] bin_centers;
		delete [] bin_counts;
	}
}

void calculate_BI(CG_MODEL_DATA* const cg, MATRIX_DATA* mat, FrameSource* const fs)
{
  initialize_first_BI_matrix(mat, cg);
  double volume = calculate_volume(fs->simulation_box_limits);
  int solution_counter = 0;
  std::list<InteractionClassComputer*>::iterator icomp_iterator;
  for(icomp_iterator = cg->icomp_list.begin(); icomp_iterator != cg->icomp_list.end(); icomp_iterator++) {
    // For every defined interaction, 
    // if that interaction has a parameter distribution
    if ( (*icomp_iterator)->ispec->output_parameter_distribution == 0) continue;
    
    // These interactions do not generate parameter distributions
    if( (*icomp_iterator)->ispec->class_type == kThreeBodyNonbonded ) continue;
  
  	// swap out icci so that matrix does not go out of bounds
  	int icci = (*icomp_iterator)->interaction_class_column_index;
  	(*icomp_iterator)->interaction_class_column_index = 0;
  	
  	// Do BI for this interaction
  	char** name = select_name((*icomp_iterator)->ispec, cg->name);
    initialize_next_BI_matrix(mat, (*icomp_iterator));
    read_interaction_file_and_build_matrix(mat, (*icomp_iterator), volume, &cg->topo_data, name);
    solve_this_BI_equation(mat, solution_counter);
    
    // restore icci
    (*icomp_iterator)->interaction_class_column_index = icci;
  }
}

void read_interaction_file_and_build_matrix(MATRIX_DATA* mat, InteractionClassComputer* const icomp, double volume, TopologyData* topo_data, char ** const name)
{ 
  // Name is correctly selected by calling function calculate_BI.
  int counter = 0;
  int* sitecounter;
  if (icomp->ispec->class_type == kPairNonbonded) {
    sitecounter = new int[topo_data->n_cg_types]();
    for(unsigned j = 0; j < topo_data->n_cg_sites; j++){
      int type = topo_data->cg_site_types[j];
      sitecounter[type-1]++;
    }
  }
    
  // Otherwise, process the data
  for (unsigned i = 0; i < icomp->ispec->defined_to_matched_intrxn_index_map.size(); i++) {
  	icomp->index_among_defined_intrxns = i; // This is OK since every defined interaction is "matched" here.
  	icomp->set_indices();
	if( icomp->ispec->class_type == kPairNonbonded ) {
	  std::vector <int> type_vector = icomp->ispec->get_interaction_types(i);
	  double num_pairs = sitecounter[type_vector[0]-1] * sitecounter[type_vector[1]-1];
	  if( type_vector[0] == type_vector[1]){
	    num_pairs -= sitecounter[type_vector[0]-1];
	  	num_pairs /= 2.0;
	  }
	  read_one_param_dist_file_pair(icomp, name, mat, i, counter,num_pairs, volume);
	} else if ( icomp->ispec->class_type == kPairBonded ) {
	  read_one_param_dist_file_pair(icomp, name, mat, i, counter, 1.0, 1.0);
	} else {
	  read_one_param_dist_file_other(icomp, name, mat, i, counter, 1.0);
	}
  }  
  if (icomp->ispec->class_type == kPairNonbonded) {
    delete [] sitecounter;
  }
}

// Read this hist file to process into Boltzmann inverted potential.
// At the same time, output an RDF file (r, g(r)).

void read_one_param_dist_file_pair(InteractionClassComputer* const icomp, char** const name, MATRIX_DATA* mat, const int index_among_defined_intrxns, int &counter, double num_of_pairs, double volume)
{
  // name is corrected selected by calling function 2x up named calculate_BI.
  std::string filename = icomp->ispec->get_basename(name, index_among_defined_intrxns, "_") + ".hist";
  std::string rdf_name = icomp->ispec->get_basename(name, index_among_defined_intrxns, "_") + ".rdf";
  FILE* curr_dist_input_file = open_file(filename.c_str(), "r");
  FILE* rdf_file = open_file(rdf_name.c_str(), "w");
  fprintf(rdf_file, "# r gofr\n"); // header.
  
  int i, counts;
  int *junk;
  double PI = 3.1415926;
  double r, potential;
  
  if (icomp->ispec->upper_cutoffs[index_among_defined_intrxns] == -1.0) return; // There is no sampling here
  
  int num_entries = (2 * (int)((icomp->ispec->upper_cutoffs[index_among_defined_intrxns] - icomp->ispec->lower_cutoffs[index_among_defined_intrxns])/icomp->ispec->get_fm_binwidth() + 0.5));
  fflush(stdout);
  
  std::array<double, DIMENSION>* derivatives = new std::array<double, DIMENSION>[num_entries - 1];
  char buffer[100];
  fgets(buffer,100,curr_dist_input_file); 
  for(i = 0; i < num_entries; i++)
    {
      int first_nonzero_basis_index;
      double normalized_counts;
      fscanf(curr_dist_input_file,"%lf %d\n",&r,&counts);
      if (counts > 0) {
        double dr = r - 0.5 * icomp->ispec->get_fm_binwidth();
      	normalized_counts = (double)(counts) * 3.0 / ( 4.0*PI*( r*r*r - dr*dr*dr) );
      	normalized_counts *= mat->normalization * volume / num_of_pairs;
      	potential = -mat->temperature*mat->boltzmann*log(normalized_counts);
      } else {
      	normalized_counts = 0.0;
      	potential = 100.0;
      	printf("Warning: Bin with no sampling encountered. Please increase bin size or use BI potenials with care.\n");
      }
      if (potential > VERYLARGE || potential < - VERYLARGE) {
      	potential = VERYLARGE;
      }
      
      fprintf(rdf_file, "%lf %lf\n", r, normalized_counts);

      icomp->fm_s_comp->calculate_basis_fn_vals(index_among_defined_intrxns, r, first_nonzero_basis_index, icomp->fm_basis_fn_vals);
      mat->accumulate_matching_forces(icomp, first_nonzero_basis_index, icomp->fm_basis_fn_vals, counter, junk, derivatives, mat);
      mat->accumulate_target_force_element(mat, counter, &potential);
      counter++;
    }
  delete [] derivatives;
  fclose(curr_dist_input_file);
  fclose(rdf_file);
}

void read_one_param_dist_file_other(InteractionClassComputer* const icomp, char** const name, MATRIX_DATA* mat, const int index_among_defined_intrxns, int &counter, double num_of_pairs)
{
  // name is corrected selected by calling function 2x up named calculate_BI.
  std::string filename = icomp->ispec->get_basename(name, index_among_defined_intrxns,  "_") + ".hist";
  std::string rdf_name = icomp->ispec->get_basename(name, index_among_defined_intrxns, "_") + ".rdf";
  FILE* curr_dist_input_file = open_file(filename.c_str(), "r");
  FILE* rdf_file = open_file(rdf_name.c_str(), "w");
  fprintf(rdf_file, "# r gofr\n"); // header.
  
  int counts;
  int *junk;
  double r;
  double potential;
  
  if (icomp->ispec->upper_cutoffs[index_among_defined_intrxns] == -1.0) return; // There is no sampling here
  int num_entries = (2 * (int)((icomp->ispec->upper_cutoffs[index_among_defined_intrxns] - icomp->ispec->lower_cutoffs[index_among_defined_intrxns])/icomp->ispec->get_fm_binwidth()));
  
  std::array<double, DIMENSION>* derivatives = new std::array<double, DIMENSION>[num_entries - 1];
  char buffer[100];
  fgets(buffer,100,curr_dist_input_file); 
  for(int i = 0; i < num_entries; i++)
    {
	  int first_nonzero_basis_index;
	  double normalized_counts;
      fscanf(curr_dist_input_file,"%lf %d\n",&r,&counts);
      if (counts > 0) {
	      normalized_counts = (double)(counts) * 2.0 * mat->normalization / num_of_pairs;
    	  potential = -mat->temperature*mat->boltzmann*log(normalized_counts);
      } else {
      	normalized_counts = 0.0;
      	potential = 100.0;
		printf("Warning: Bin with no sampling encountered. Please increase bin size or use BI potenials with care.\n");
      }
	  if (potential > VERYLARGE || potential < - VERYLARGE) {
      	potential = VERYLARGE;
      }
      
      icomp->fm_s_comp->calculate_basis_fn_vals(index_among_defined_intrxns, r, first_nonzero_basis_index, icomp->fm_basis_fn_vals);
      mat->accumulate_matching_forces(icomp, first_nonzero_basis_index, icomp->fm_basis_fn_vals, counter, junk, derivatives, mat);
      mat->accumulate_target_force_element(mat, counter, &potential);
      counter++;
    }
  delete [] derivatives;
  fclose(curr_dist_input_file);
  fclose(rdf_file);
}

bool any_active_parameter_distributions(CG_MODEL_DATA* const cg) {
	std::list<InteractionClassSpec*>::iterator iclass_iterator;
	for(iclass_iterator = cg->iclass_list.begin(); iclass_iterator != cg->iclass_list.end(); iclass_iterator++) {
        if((*iclass_iterator)->output_parameter_distribution != 0) {
        	return true;
        }
    }
    return false;
}

void screen_interactions_by_distribution(CG_MODEL_DATA* const cg) {
	std::list<InteractionClassSpec*>::iterator iclass_iterator;
	for(iclass_iterator = cg->iclass_list.begin(); iclass_iterator != cg->iclass_list.end(); iclass_iterator++) {
        if((*iclass_iterator)->output_parameter_distribution == 0) {
        	(*iclass_iterator)->n_to_force_match = 0;
        	(*iclass_iterator)->n_tabulated = 0;
        	(*iclass_iterator)->interaction_column_indices[0] = 0;
        } else {
        	(*iclass_iterator)->set_basis_type(kBSplineAndDeriv);
        }
    }
}

void free_name(CG_MODEL_DATA* const cg)
{
    // Free data after output.
    for (int i = 0; i < cg->n_cg_types; i++) delete [] cg->name[i];
    delete [] cg->name;
}
