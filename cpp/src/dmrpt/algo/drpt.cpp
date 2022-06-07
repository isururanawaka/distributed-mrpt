#include <cblas.h>
#include <stdio.h>
#include "drpt.hpp"
#include "../math/matrix_multiply.hpp"
#include <vector>
#include <random>
#include <mpi.h>
#include <string>
#include <iostream>
#include <omp.h>


using namespace std;

dmrpt::DRPT::DRPT(double *projected_matrix, int rows, int cols, dmrpt::StorageFormat storageFormat) {
    this->tree_depth = cols;
    this->rows = rows;
    this->cols = cols;
    this->storageFormat = storageFormat;
    this->projected_matrix = projected_matrix;
    this->data = vector < vector < double >> (cols);
    this->indices = vector<int>(rows);

}


void dmrpt::DRPT::count_leaf_sizes(int datasize, int level, int depth, std::vector<int> &out_leaf_sizes) {
    if (level == depth) {
        out_leaf_sizes.push_back(datasize);
        return;
    }

    this->count_leaf_sizes(datasize - datasize / 2, level + 1, depth, out_leaf_sizes);
    this->count_leaf_sizes(datasize / 2, level + 1, depth, out_leaf_sizes);
}


void dmrpt::DRPT::count_first_leaf_indices(std::vector<int> &indices, int datasize, int depth) {
    std::vector<int> leaf_sizes;
    this->count_leaf_sizes(datasize, 0, depth, leaf_sizes);
    indices = std::vector<int>(leaf_sizes.size() + 1);
    indices[0] = 0;
    for (int i = 0; i < (int) leaf_sizes.size(); ++i)
        indices[i + 1] = indices[i] + leaf_sizes[i];
}

void dmrpt::DRPT::count_first_leaf_indices_all(std::vector <std::vector<int>> &indices, int datasize, int depth_max) {
    for (int d = 0; d <= depth_max; ++d) {
        std::vector<int> idx;
        this->count_first_leaf_indices(idx, datasize, d);
        indices.push_back(idx);
    }
}


void dmrpt::DRPT::grow_local_tree(int rank) {

    if (tree_depth <= 0 || tree_depth > log2(rows)) {
        throw std::out_of_range(" depth should be in range [1,....,log2(rows)]");
    }
    int total_split_size = 1 << (tree_depth + 1);
    this->splits = vector<double>(total_split_size);

    if (dmrpt::StorageFormat::RAW == storageFormat) {

        this->count_first_leaf_indices_all(this->leaf_first_indices_all, rows, this->tree_depth);
        this->leaf_first_indices = this->leaf_first_indices_all[this->tree_depth];

#pragma omp parallel for shared(this->data)
        for (int i = 0; i < cols; i++) {
            this->data[i] = vector<double>(rows);
            for (int j = 0; j < rows; j++) {
                this->data[i][j] = this->projected_matrix[i + j * cols];
            }
        }

        iota(this->indices.begin(), this->indices.end(), 0);
        grow_local_subtree(this->indices.begin(), this->indices.end(), 0, 0, rank);

    }


}

void dmrpt::DRPT::grow_local_subtree(std::vector<int>::iterator begin, std::vector<int>::iterator end,
                                     int depth, int i, int rank) {
    int datasize = end - begin;
    int id_left = 2 * i + 1;
    int id_right = id_left + 1;

    if (depth == tree_depth) {
        return;
    }

    std::nth_element(begin, begin + datasize / 2, end, [this, depth](int a, int b) -> bool {
        return this->data[depth][a] < this->data[depth][b];
    });

    auto mid = end - datasize / 2;


    if (datasize % 2) {
        this->splits[i] = this->data[depth][*(mid - 1)];

    } else {

        auto left = std::max_element(begin, mid, [this, depth](int a, int b) -> bool {
            return this->data[depth][a] < this->data[depth][b];
        });

        this->splits[i] = (this->data[depth][*mid] + this->data[depth][*left]) / 2.0;
    }

    grow_local_subtree(begin, mid, depth + 1, id_left, rank);

    grow_local_subtree(mid, end, depth + 1, id_right, rank);

}

vector <vector<double>> dmrpt::DRPT::query(double *queryP, int rank) {

    int idx = 0;
    vector <vector<double>> vec;

//    #pragma omp parallel for
    for (int i = 0; i < this->tree_depth; ++i) {

        int id_left = 2 * idx + 1;
        int id_right = id_left + 1;
        double split_point = this->splits[idx];
        if (queryP[i] <= split_point) {
            idx = id_left;
        } else {
            idx = id_right;
        }

    }

    int selected_leaf = idx - (1 << this->tree_depth) + 1;

    int leaf_begin = this->leaf_first_indices[selected_leaf];
    int leaf_end = this->leaf_first_indices[selected_leaf + 1];

    for (int k = leaf_begin; k < leaf_end; ++k) {
        int orginal_data_index = this->indices[k];
        cout << " Rank " << rank << "leaf "<< selected_leaf<<" idx "<< idx <<" k "<<k<<  " selected value " << orginal_data_index << " tree depth " << this->tree_depth
             << endl;
    }

    return vec;
}




