/**
** Created by Zhijian QIAO.
** UAV Group, Hong Kong University of Science and Technology
** email: zqiaoac@connect.ust.hk
**/
#include "fpfh_matcher.h"

namespace teaser{

    void Matcher::normalizePoints(bool use_absolute_scale) {
        if (use_absolute_scale)
            return;
        int num = 2;
        float scale = 0;

        means_.clear();

        for (int i = 0; i < num; ++i) {
            float max_scale = 0;

            // compute mean
            Eigen::Vector3f mean;
            mean.setZero();

            int npti = pointcloud_[i].size();
            for (int ii = 0; ii < npti; ++ii) {
                Eigen::Vector3f p(pointcloud_[i].at(ii).x, pointcloud_[i].at(ii).y, pointcloud_[i].at(ii).z);
                mean = mean + p;
            }
            mean = mean / npti;
            means_.push_back(mean);

            for (int ii = 0; ii < npti; ++ii) {
                pointcloud_[i].at(ii).x -= mean(0);
                pointcloud_[i].at(ii).y -= mean(1);
                pointcloud_[i].at(ii).z -= mean(2);
            }

            // compute scale
            for (int ii = 0; ii < npti; ++ii) {
                Eigen::Vector3f p(pointcloud_[i].at(ii).x, pointcloud_[i].at(ii).y, pointcloud_[i].at(ii).z);
                float temp = p.norm(); // because we extract mean in the previous stage.
                if (temp > max_scale) {
                    max_scale = temp;
                }
            }

            if (max_scale > scale) {
                scale = max_scale;
            }
        }

        // mean of the scale variation
        if (use_absolute_scale) {
            global_scale_ = 1.0f;
        } else {
            global_scale_ = scale; // second choice: we keep the maximum scale.
        }

        if (global_scale_ != 1.0f) {
            for (int i = 0; i < num; ++i) {
                int npti = pointcloud_[i].size();
                for (int ii = 0; ii < npti; ++ii) {
                    pointcloud_[i].at(ii).x /= global_scale_;
                    pointcloud_[i].at(ii).y /= global_scale_;
                    pointcloud_[i].at(ii).z /= global_scale_;
                }
            }
        }
    }

    void Matcher::advancedMatching(bool use_crosscheck, bool use_tuple_test, float tuple_scale) {

        // use_crosscheck: if true, true correspondences should be mutual nearest neighbors.
        // use_tuple_test: if true, sample random tuple correspondences (sample very much), and check if the ratio of distances is less than scale

        int fi = 0; // source idx (larger pointcloud)
        int fj = 1; // destination idx (smaller pointcloud)

        bool swapped = false;

        // swap the pointclouds if the first one is less than the second one
        if (pointcloud_[fj].size() > pointcloud_[fi].size()) {
            int temp = fi;
            fi = fj;
            fj = temp;
            swapped = true;
        }

        int nPti = pointcloud_[fi].size();
        int nPtj = pointcloud_[fj].size();

        ///////////////////////////
        /// Build FLANNTREE
        ///////////////////////////
        KDTree feature_tree_i(flann::KDTreeSingleIndexParams(15));
        buildKDTree(features_[fi], &feature_tree_i);

        KDTree feature_tree_j(flann::KDTreeSingleIndexParams(15));
        buildKDTree(features_[fj], &feature_tree_j);

        std::vector<int> corres_K, corres_K2;
        std::vector<float> dis;
        std::vector<int> ind;

        std::vector<std::pair<int, int>> corres;
        std::vector<std::pair<int, int>> corres_cross;
        std::vector<std::pair<int, int>> corres_ij;
        std::vector<std::pair<int, int>> corres_ji;

        ///////////////////////////
        /// INITIAL MATCHING
        ///////////////////////////
        std::vector<int> i_to_j(nPti, -1);
        for (int j = 0; j < nPtj; j++) {
            searchKDTree(&feature_tree_i, features_[fj][j], corres_K, dis, 1);
            int i = corres_K[0];
            if (i_to_j[i] == -1) {
                searchKDTree(&feature_tree_j, features_[fi][i], corres_K, dis, 1);
                int ij = corres_K[0];
                i_to_j[i] = ij;
            }
            corres_ji.push_back(std::pair<int, int>(i, j));
        }

        for (int i = 0; i < nPti; i++) {
            if (i_to_j[i] != -1)
                corres_ij.push_back(std::pair<int, int>(i, i_to_j[i]));
        }

        int ncorres_ij = corres_ij.size();
        int ncorres_ji = corres_ji.size();

        // corres = corres_ij + corres_ji;
        for (int i = 0; i < ncorres_ij; ++i)
            corres.push_back(std::pair<int, int>(corres_ij[i].first, corres_ij[i].second));
        for (int j = 0; j < ncorres_ji; ++j)
            corres.push_back(std::pair<int, int>(corres_ji[j].first, corres_ji[j].second));

        ///////////////////////////
        /// CROSS CHECK
        /// input : corres_ij, corres_ji
        /// output : corres
        ///////////////////////////
        if (use_crosscheck) {
            //std::cout << "CROSS CHECK" << std::endl;
            // build data structure for cross check
            corres.clear();
            corres_cross.clear();
            std::vector<std::vector<int>> Mi(nPti);
            std::vector<std::vector<int>> Mj(nPtj);

            int ci, cj;
            for (int i = 0; i < ncorres_ij; ++i) {
                ci = corres_ij[i].first;
                cj = corres_ij[i].second;
                Mi[ci].push_back(cj);
            }
            for (int j = 0; j < ncorres_ji; ++j) {
                ci = corres_ji[j].first;
                cj = corres_ji[j].second;
                Mj[cj].push_back(ci);
            }

            // cross check
            for (int i = 0; i < nPti; ++i) {
                for (int ii = 0; ii < Mi[i].size(); ++ii) {
                    int j = Mi[i][ii];
                    for (int jj = 0; jj < Mj[j].size(); ++jj) {
                        if (Mj[j][jj] == i) {
                            corres.push_back(std::pair<int, int>(i, j));
                            corres_cross.push_back(std::pair<int, int>(i, j));
                        }
                    }
                }
            }
        } else {
            std::cout << "Skipping Cross Check." << std::endl;
        }

        ///////////////////////////
        /// TUPLE CONSTRAINT
        /// input : corres
        /// output : corres
        ///////////////////////////
        if (use_tuple_test && tuple_scale != 0) {
            //std::cout << "TUPLE CONSTRAINT" << std::endl;
            srand(time(NULL));
            int rand0, rand1, rand2;
            int idi0, idi1, idi2;
            int idj0, idj1, idj2;
            float scale = tuple_scale;
            int ncorr = corres.size();
            int number_of_trial = ncorr * 100;
            std::vector<std::pair<int, int>> corres_tuple;

            for (int i = 0; i < number_of_trial; i++) {
                rand0 = rand() % ncorr;
                rand1 = rand() % ncorr;
                rand2 = rand() % ncorr;

                idi0 = corres[rand0].first;
                idj0 = corres[rand0].second;
                idi1 = corres[rand1].first;
                idj1 = corres[rand1].second;
                idi2 = corres[rand2].first;
                idj2 = corres[rand2].second;

                // collect 3 points from i-th fragment
                Eigen::Vector3f pti0 = {pointcloud_[fi].at(idi0).x, pointcloud_[fi].at(idi0).y,
                                        pointcloud_[fi].at(idi0).z};
                Eigen::Vector3f pti1 = {pointcloud_[fi].at(idi1).x, pointcloud_[fi].at(idi1).y,
                                        pointcloud_[fi].at(idi1).z};
                Eigen::Vector3f pti2 = {pointcloud_[fi].at(idi2).x, pointcloud_[fi].at(idi2).y,
                                        pointcloud_[fi].at(idi2).z};

                float li0 = (pti0 - pti1).norm();
                float li1 = (pti1 - pti2).norm();
                float li2 = (pti2 - pti0).norm();

                // collect 3 points from j-th fragment
                Eigen::Vector3f ptj0 = {pointcloud_[fj].at(idj0).x, pointcloud_[fj].at(idj0).y,
                                        pointcloud_[fj].at(idj0).z};
                Eigen::Vector3f ptj1 = {pointcloud_[fj].at(idj1).x, pointcloud_[fj].at(idj1).y,
                                        pointcloud_[fj].at(idj1).z};
                Eigen::Vector3f ptj2 = {pointcloud_[fj].at(idj2).x, pointcloud_[fj].at(idj2).y,
                                        pointcloud_[fj].at(idj2).z};

                float lj0 = (ptj0 - ptj1).norm();
                float lj1 = (ptj1 - ptj2).norm();
                float lj2 = (ptj2 - ptj0).norm();

                if ((li0 * scale < lj0) && (lj0 < li0 / scale) && (li1 * scale < lj1) &&
                    (lj1 < li1 / scale) && (li2 * scale < lj2) && (lj2 < li2 / scale)) {
                    corres_tuple.push_back(std::pair<int, int>(idi0, idj0));
                    corres_tuple.push_back(std::pair<int, int>(idi1, idj1));
                    corres_tuple.push_back(std::pair<int, int>(idi2, idj2));
                }
            }
            corres.clear();

            for (size_t i = 0; i < corres_tuple.size(); ++i)
                corres.push_back(std::pair<int, int>(corres_tuple[i].first, corres_tuple[i].second));
        } else {
            //std::cout << "Skipping Tuple Constraint." << std::endl;
        }

        if (swapped) {
            std::vector<std::pair<int, int>> temp;
            for (size_t i = 0; i < corres.size(); i++)
                temp.push_back(std::pair<int, int>(corres[i].second, corres[i].first));
            corres.clear();
            corres = temp;
        }
        corres_ = corres;

        ///////////////////////////
        /// ERASE DUPLICATES
        /// input : corres_
        /// output : corres_
        ///////////////////////////
        std::sort(corres_.begin(), corres_.end());
        corres_.erase(std::unique(corres_.begin(), corres_.end()), corres_.end());
    }

    template<typename T>
    void Matcher::buildKDTree(const std::vector<T> &data, Matcher::KDTree *tree) {
        int rows, dim;
        rows = (int) data.size();
        dim = (int) data[0].size();
        std::vector<float> dataset(rows * dim);
        flann::Matrix<float> dataset_mat(&dataset[0], rows, dim);
        for (int i = 0; i < rows; i++)
            for (int j = 0; j < dim; j++)
                dataset[i * dim + j] = data[i][j];
        KDTree temp_tree(dataset_mat, flann::KDTreeSingleIndexParams(15));
        temp_tree.buildIndex();
        *tree = temp_tree;
    }

    template<typename T>
    void Matcher::searchKDTree(Matcher::KDTree *tree, const T &input, std::vector<int> &indices,
                      std::vector<float> &dists, int nn) {
        int rows_t = 1;
        int dim = input.size();

        std::vector<float> query;
        query.resize(rows_t * dim);
        for (int i = 0; i < dim; i++)
            query[i] = input(i);
        flann::Matrix<float> query_mat(&query[0], rows_t, dim);

        indices.resize(rows_t * nn);
        dists.resize(rows_t * nn);
        flann::Matrix<int> indices_mat(&indices[0], rows_t, nn);
        flann::Matrix<float> dists_mat(&dists[0], rows_t, nn);

        tree->knnSearch(query_mat, indices_mat, dists_mat, nn, flann::SearchParams(128));
    }
}