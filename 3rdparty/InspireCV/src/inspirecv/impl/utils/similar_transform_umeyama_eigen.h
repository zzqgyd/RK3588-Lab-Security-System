#ifndef FACE_PREPROCESS_EIGEN_H
#define FACE_PREPROCESS_EIGEN_H

#include <Eigen/Dense>
#include <Eigen/SVD>
#include <iostream>
#include "check.h"

namespace inspirecv  {

using Matrix = Eigen::MatrixXf;
using Vector = Eigen::VectorXf;

// Calculate mean value for each column of matrix
Matrix MeanAxis0(const Matrix& src) {
    Matrix mean(1, src.cols());
    mean = src.colwise().mean();
    return mean;
}

// Subtract vector from each row of matrix
Matrix ElementwiseMinus(const Matrix& A, const Matrix& B) {
    return A.rowwise() - B.row(0);
}

// Calculate variance
Matrix VarAxis0(const Matrix& src) {
    Matrix temp = ElementwiseMinus(src, MeanAxis0(src));
    return (temp.array() * temp.array()).colwise().mean();
}

// Calculate matrix rank
int MatrixRank(const Matrix& M) {
    Eigen::JacobiSVD<Matrix> svd(M);
    double threshold = 1e-4 * std::max(M.cols(), M.rows()) * svd.singularValues().array().abs()(0);
    return (svd.singularValues().array() > threshold).count();
}


// Compute similarity transform using Umeyama algorithm
Eigen::Matrix3f SimilarTransform(const Matrix& src, const Matrix& dst) {
    int num = src.rows();
    int dim = src.cols();
    
    // Calculate means
    Matrix src_mean = MeanAxis0(src);
    Matrix dst_mean = MeanAxis0(dst);
    
    // Center the points
    Matrix src_demean = ElementwiseMinus(src, src_mean);
    Matrix dst_demean = ElementwiseMinus(dst, dst_mean);
    
    // Compute covariance matrix A = (dst_demean.T * src_demean) / num
    Matrix A = (dst_demean.transpose() * src_demean) / static_cast<float>(num);
    
    // Initialize transformation matrix
    Eigen::Matrix3f T = Eigen::Matrix3f::Identity();
    
    // SVD decomposition
    Eigen::JacobiSVD<Matrix> svd(A, Eigen::ComputeFullU | Eigen::ComputeFullV);
    Matrix U = svd.matrixU();
    Matrix V = svd.matrixV();
    Vector S = svd.singularValues();
    
    // Handle reflection case
    Vector d = Vector::Ones(dim);
    if (A.determinant() < 0) {
        d(dim - 1) = -1;
    }
    
    int rank = MatrixRank(A);
    
    if (rank == 0) {
        INSPIRECV_LOG(ERROR) << "Error: rank == 0";
        return T;
    } 
    else if (rank == dim - 1) {
        if (U.determinant() * V.determinant() > 0) {
            T.block(0, 0, dim, dim) = U * V.transpose();
        } 
        else {
            d(dim - 1) = -1;
            T.block(0, 0, dim, dim) = U * d.asDiagonal() * V.transpose();
        }
    } 
    else {
        T.block(0, 0, dim, dim) = U * d.asDiagonal() * V.transpose();
    }
    
    // Calculate scaling factor
    Matrix var = VarAxis0(src_demean);
    float scale = 1.0f / var.sum() * (d.array() * S.array()).sum();
    
    // Calculate translation vector
    Vector t = dst_mean.row(0).transpose() - scale * T.block(0, 0, dim, dim) * src_mean.row(0).transpose();
    T(0, 2) = t(0);
    T(1, 2) = t(1);
    
    // Apply scaling
    T.block(0, 0, dim, dim) *= scale;
    
    return T;
}

// Overloaded SimilarTransform that takes vectors of floats
std::vector<float> SimilarTransform(const std::vector<float>& src_vec, const std::vector<float>& dst_vec) {
    // Check if vectors have same size and contain pairs of coordinates
    INSPIRECV_CHECK_EQ(src_vec.size(), dst_vec.size());
    INSPIRECV_CHECK_EQ(src_vec.size() % 2, 0);

    // Convert vectors to matrices
    int num_points = src_vec.size() / 2;
    Matrix src(num_points, 2);
    Matrix dst(num_points, 2);

    for (int i = 0; i < num_points; i++) {
        src(i, 0) = src_vec[2*i];     // x coordinate
        src(i, 1) = src_vec[2*i + 1]; // y coordinate
        dst(i, 0) = dst_vec[2*i];     // x coordinate 
        dst(i, 1) = dst_vec[2*i + 1]; // y coordinate
    }

    // Call the matrix version of SimilarTransform
    Eigen::Matrix3f transform_matrix = SimilarTransform(src, dst);
    
    // Convert matrix to vector in row-major order
    std::vector<float> result;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            result.push_back(transform_matrix(i,j));
        }
    }
    
    return result;
}



}  // namespace FacePreprocess

#endif // FACE_PREPROCESS_EIGEN_H