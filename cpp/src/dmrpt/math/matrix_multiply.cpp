#include <cblas.h>
#include <stdio.h>
#include "matrix_multiply.hpp"
#include <vector>
#include <random>
#include <mpi.h>
#include <string>
#include <iostream>
#include <omp.h>

using namespace std;


double *dmrpt::MathOp::multiply_mat(double *A, double *B, int A_rows, int B_cols, int A_cols, int alpha) {
    int result_size = A_cols * B_cols;
    double *result = (double *) malloc(sizeof(double) * result_size);
    for (int k = 0; k < result_size; k++) {
        result[k] = 1;
    }
    cblas_dgemm(CblasRowMajor, CblasTrans, CblasNoTrans, A_cols, B_cols, A_rows, alpha, A, A_cols, B, B_cols, 0.0,
                result, B_cols);

    return result;
}

double *dmrpt::MathOp::build_sparse_local_random_matrix(int rows, int cols, float density) {
    double *A;
    int size = rows * cols;
    A = (double *) malloc(sizeof(double) * size);

    int seed = 0;
    std::random_device rd;
    int s = seed ? seed : rd();
    std::mt19937 gen(s);
    std::uniform_real_distribution<float> uni_dist(0, 1);
    std::normal_distribution<float> norm_dist(0, 1);

    //follow row major order
    for (int j = 0; j < rows; ++j) {
        for (int i = 0; i < cols; ++i) {
            if (uni_dist(gen) > density) {
                A[i + j * cols] = 0.0;
            } else {
                A[i + j * cols] = (double) norm_dist(gen);
            }
        }
    }
    return A;
}

double *dmrpt::MathOp::build_sparse_projection_matrix(int rank, int world_size, int total_dimension, int levels,
                                                      float density) {

    double *global_project_matrix;
    int local_rows;
    int length = total_dimension / world_size;
    if (rank < world_size - 1) {
        local_rows = length;
    } else if (rank == world_size - 1) {
        local_rows = total_dimension - rank * (length);
    }
    double *local_sparse_matrix = this->build_sparse_local_random_matrix(local_rows, levels, density);

    global_project_matrix = (double *) malloc(sizeof(double) * total_dimension * levels);


    int my_start, my_end;
    if (rank < world_size - 1) {
        my_start = rank * length * levels;
        my_end = my_start + length * levels - 1;
    } else if (rank == world_size - 1) {
        my_start = rank * length * levels;
        my_end = total_dimension * levels - 1;
    }

    for (int i = my_start; i <= my_end; i++) {
        global_project_matrix[i] = local_sparse_matrix[i - my_start];
    }

    int my_total = local_rows * levels;

    int *counts = new int[world_size];
    int *disps = new int[world_size];

    for (int i = 0; i < world_size - 1; i++)
        counts[i] = length * levels;
    counts[world_size - 1] = total_dimension * levels - length * levels * (world_size - 1);

    disps[0] = 0;
    for (int i = 1; i < world_size; i++)
        disps[i] = disps[i - 1] + counts[i - 1];

    MPI_Allgatherv(MPI_IN_PLACE, 0, MPI_DOUBLE,
                   global_project_matrix, counts, disps, MPI_DOUBLE, MPI_COMM_WORLD);

    delete[] disps;
    delete[] counts;
    free(local_sparse_matrix);

    return global_project_matrix;

}

double *dmrpt::MathOp::convert_to_row_major_format(vector <vector<double>> data) {
    int cols = data.size();
    int rows = data[0].size();
    int total_size = cols * rows;

    double *arr = (double *) malloc(sizeof(double) * total_size);
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            arr[j + i * cols] = 0.0;
        }
    }

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            arr[j + i * cols] = data[j][i];
        }

    }
    return arr;
}

double *dmrpt::MathOp::distributed_mean(double *data, int local_rows, int local_cols, int total_elements,
                                        dmrpt::StorageFormat format, int rank) {
    int size = local_rows * local_cols;
    double *sums = (double *) malloc(sizeof(double) * local_cols);
    double *gsums = (double *) malloc(sizeof(double) * local_cols);
    for (int i = 0; i < local_cols; i++) {
        sums[i] = 0.0;
    }
    if (format == dmrpt::StorageFormat::RAW) {
        for (int i = 0; i < local_cols; i++) {
            double sum = 0.0;
            for (int j = 0; j < local_rows; j++) {
                sum = sum + data[i + j * local_cols];
            }
            sums[i] = sum;
        }
    }
    MPI_Allreduce(sums, gsums, local_cols, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

    for (int i = 0; i < local_cols; i++) {
        gsums[i] = gsums[i] / total_elements;
    }
    free(sums);
    return gsums;
}

double *dmrpt::MathOp::distributed_variance(double *data, int local_rows, int local_cols, int total_elements,
                                            dmrpt::StorageFormat format, int rank) {
    double *means = this->distributed_mean(data, local_rows, local_cols, total_elements, format, rank);
    int size = local_rows * local_cols;
    double *var = (double *) malloc(sizeof(double) * local_cols);
    double *gvariance = (double *) malloc(sizeof(double) * local_cols);
    for (int i = 0; i < local_cols; i++) {
        var[i] = 0.0;
    }
    if (format == dmrpt::StorageFormat::RAW) {
        for (int i = 0; i < local_cols; i++) {
            double sum = 0.0;
            for (int j = 0; j < local_rows; j++) {
                double diff = (data[i + j * local_cols] - means[i]);
                sum = sum + (diff * diff);
            }
            var[i] = sum;
        }
    }
    MPI_Allreduce(var, gvariance, local_cols, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

    for (int i = 0; i < local_cols; i++) {
        gvariance[i] = gvariance[i] / (total_elements - 1);
//        cout<<"Rank "<<rank<<"Variance "<< gvariance[i]<<" "<<endl;
    }
    free(means);
    return gvariance;
}


double *
dmrpt::MathOp::distributed_median(double *data, int local_rows, int local_cols, int total_elements, int no_of_bins,
                                  dmrpt::StorageFormat format, int rank) {
    double *means = this->distributed_mean(data, local_rows, local_cols, total_elements, format, rank);
    double *variance = this->distributed_variance(data, local_rows, local_cols, total_elements, format, rank);
    double *medians = (double *)malloc(sizeof (double )*local_cols);
    int size = local_rows * local_cols;

    int std1 = 4, std2 = 2, std3 = 1;

    for(int k=0;k<local_cols;k++){
        medians[k]=INFINITY;
    }

    if (format == dmrpt::StorageFormat::RAW) {

        for (int i = 0; i < local_cols ; i++) {
            double mu = means[i];
            double sigma = variance[i];
            sigma = sqrt(sigma);
            cout<<"Col "<<i<<" mean "<<mu<< " variance "<<sigma<<endl;
            double val = 0.0;
            int factor = (int) ceil(no_of_bins * 1.0 / (std1 + std2 + std3));

            vector<double> distribution(2 * factor * (std1 + std2 + std3) + 2, 0);

            vector<int> frequency(2 * factor * (std1 + std2 + std3) + 2, 0);

            int start1 = factor * (std1 + std2 + std3);
            double step1 = sigma / (2 * pow(2, std1 * factor) - 2);

            for (int k = start1, j = 1; k < start1 + std1 * factor; k++, j++) {
                double rate = j * step1;
                distribution[k] = mu + rate;
                distribution[start1 - j] = mu - rate;
            }

            int start2 = start1 + std1 * factor;
            int rstart2 = start1 - std1 * factor;
            double step2 = sigma / (2 * pow(2, std2 * factor) - 2);

            for (int k = start2, j = 1; k < start2 + std2 * factor; k++, j++) {
                double rate = sigma + j * step2;
                distribution[k] = mu + rate;
                distribution[rstart2 - j] = mu - rate;
            }

            int start3 = start2 + std2 * factor;
            int rstart3 = rstart2 - std2 * factor;
            double step3 = sigma / (2 * pow(2, std3 * factor) - 2);

            for (int k = start3, j = 1; k < start3 + std3 * factor; k++, j++) {
                double rate = 2 * sigma + j * step3;
                distribution[k] = mu + rate;
                distribution[rstart3 - j] = mu - rate;
            }


            for (int k = 0; k < local_rows; k++) {
                int flag = 1;
                for (int j = 1; j < 2 * no_of_bins + 2; j++) {
                    double dval = data[i+ k * local_cols];
                    if (distribution[j - 1] < dval && distribution[j] >= dval) {
                        flag = 0;
                        frequency[j] += 1;
                    }
                }
                if (flag) {
                    frequency[0] += 1;
                }
            }

            int *gfrequency = (int *) malloc(sizeof(int) * distribution.size());
            int *freqarray = (int *) malloc(sizeof(int) * distribution.size());
            for (int k = 0; k < distribution.size(); k++) {
                freqarray[k] = frequency[k];
                gfrequency[k] = 0;
            }



            MPI_Allreduce(freqarray, gfrequency, distribution.size(), MPI_INT, MPI_SUM, MPI_COMM_WORLD);

//            for(int k=0;k<distribution.size();k++){
//                if (rank==0) {
//                    cout << "k "<<k<<" distribution " << distribution[k] << " Frequency " << gfrequency[k] << endl;
//                }
//            }

            double cfreq = 0;
            double cper = 0;
            int selected_index = -1;
            for (int k = 1; k < distribution.size(); k++) {
                cfreq += gfrequency[k];
                cper += gfrequency[k]*100/total_elements;
                if (cper > 50) {
                    selected_index = k;
                    break;
                }
            }

            int count = gfrequency[selected_index];


            double median = distribution[selected_index-1] +
                    ((total_elements/2-(cfreq-count))/count)*(distribution[selected_index]- distribution[selected_index-1]);
            medians[i]=median;

            free(gfrequency);
            free(freqarray);

        }
    }

    return medians;
}

double dmrpt::MathOp::calculate_distance(vector<double> data, vector<double> query) {

    std::vector<double>	auxiliary(data.size());

    std::transform (data.begin(), data.end(), query.begin(), std::back_inserter(auxiliary),//
                    [](double element1, double element2) {return pow((element1-element2),2);});

    double  value =  sqrt(std::accumulate(auxiliary.begin(), auxiliary.end(), 0.0));
    data.clear();
    query.clear();
    auxiliary.clear();
    return  value;
}


