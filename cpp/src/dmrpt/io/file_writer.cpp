#include <iostream>#include <fstream>#include <string>#include <vector>#include <sstream>#include "mpi.h"#include <cstring>#include <cmath>#include <map>#include "../algo/drpt_local.hpp"#include "file_writer.hpp"#include <algorithm>using namespace std;unsigned get_number_of_digits (unsigned i){	return i > 0 ? (int) log10 ((double) i) + 1 : 1;}template<typename IT> IT** dmrpt::FileWriter<IT>::alloc2d(int rows, int cols){	int size = rows*cols;	IT *data = (IT*)malloc(size*sizeof(IT));	IT **array = (IT**)malloc(rows*sizeof(IT*));	for (int i=0; i<rows; i++)		array[i] = &(data[i*cols]);	return array;}template<typename IT>void dmrpt::FileWriter<IT>::mpi_write_edge_list(std::map<int, vector<dmrpt::DataPoint>> &data_points, string output_path, int  nn,		int rank, int world_size, bool filter_self_edges) {	MPI_Offset offset;	MPI_File   file;	MPI_Status status;	MPI_Datatype num_as_string;	MPI_Datatype localarray;	IT **data;	char *const fmt="%d ";	char *const endfmt="%d\n";	int totalchars=0;	int local_rows = 0;	for (int i=0; i<data_points.size(); i++)	{		vector<dmrpt::DataPoint> vec = data_points[i];		if(filter_self_edges){			vec.erase(remove_if(vec.begin(),vec.end(),[&](dmrpt::DataPoint dataPoint) {			  return dataPoint.src_index == dataPoint.index;}), vec.end());		}		int l_rows =  (vec.size() >= (nn-1) ? (nn-1) : vec.size());		local_rows += l_rows;	}	int cols = 2;	int *send_counts = new int[world_size]();	int *recv_counts = new int[world_size]();	for(int i=0;i<world_size;i++){		send_counts[i]=local_rows;	}	MPI_Alltoall (send_counts, 1, MPI_INT, recv_counts, 1,			MPI_INT, MPI_COMM_WORLD);	int startrow =0;	int global_rows =0;	for(int j=0;j<world_size;j++){		if (j<rank){			startrow += recv_counts[j];		}		global_rows += recv_counts[j];	}//	int endrow = startrow+local_rows-1;	cout << "rank "<<rank<<"global_rows" <<global_rows<<" local_rows "<<local_rows<<endl;	data = alloc2d(local_rows,cols);	cout << "rank "<<rank<<"data allocation completed" << endl;	int ind = 0;	for (int i=0; i<data_points.size(); i++)	{		vector<DataPoint> vec = data_points[i];		for (int j = 0; j < (vec.size() >= (nn-1) ? (nn-1) : vec.size()); j++)		{			if (ind < local_rows)			{				data[ind][0] =  vec[j].src_index +1;				data[ind][1] = vec[j].index +1;				totalchars = totalchars+ get_number_of_digits(vec[j].src_index +1)+get_number_of_digits(vec[j].index +1)+2;				ind++;			}		}	}	cout << "rank "<<rank<<"data filling completed final total"<<ind << endl;//	MPI_Type_contiguous(charspernum, MPI_CHAR, &num_as_string);//	MPI_Type_commit(&num_as_string);	cout << "rank "<<rank<<"total chars"<<totalchars << endl;	/* convert our data into txt */	char *data_as_txt = (char*)malloc(totalchars*sizeof(char));	int count = 0;	int current_total_chars=0;	for (int i=0; i<local_rows; i++) {		for (int j=0; j<cols-1; j++) {			int local_chars = get_number_of_digits(data[i][j]);			sprintf(&data_as_txt[current_total_chars],fmt,data[i][j]);			current_total_chars =current_total_chars+local_chars+1;		}		int local_chars = get_number_of_digits(data[i][cols-1]);		sprintf(&data_as_txt[current_total_chars],endfmt,data[i][cols-1]);		current_total_chars =current_total_chars +local_chars+1;	}	cout << "rank "<<rank<<"sprintf completed total_chars_completed "<<current_total_chars << endl;	/* create a type describing our piece of the array */	int globalsizes[2] = {global_rows, cols};	int localsizes [2] = {local_rows, cols};	int starts[2]      = {startrow, 0};	int order          = MPI_ORDER_C;	MPI_Type_create_subarray(2, globalsizes, localsizes, starts, order, MPI_CHAR, &localarray);	MPI_Type_commit(&localarray);	/* open the file, and set the view */	MPI_File_open(MPI_COMM_WORLD, output_path.c_str(),			MPI_MODE_CREATE|MPI_MODE_WRONLY,			MPI_INFO_NULL, &file);	MPI_File_set_view(file, 0,  MPI_CHAR, localarray,			"native", MPI_INFO_NULL);	MPI_File_write_all(file, data_as_txt, totalchars*sizeof(char), MPI_CHAR, &status);	MPI_File_close(&file);	cout << "rank "<<rank<<"file writing completed for path"<<output_path << endl;	MPI_Type_free(&localarray);//	MPI_Type_free(&num_as_string);	free(data[0]);	free(data);	free(data_as_txt);}template class dmrpt::FileWriter<int>;