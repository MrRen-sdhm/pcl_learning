#include <iostream>
#include <string>
#include <pcl/io/pcd_io.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <Eigen/Core>
#include <pcl/common/transforms.h>
#include <pcl/common/common.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/features/normal_3d_omp.h>

using namespace std;
typedef pcl::PointXYZ PointType;

void save_normals(const string& filename, pcl::PointCloud<pcl::Normal>::Ptr normals_out) {
    /// save normals use PointXYZ formate

    std::ofstream myfile;
    myfile.open(filename);

    myfile << "# .PCD v0.7 - Point Cloud Data file format" << "\n";
    myfile << "VERSION 0.7" << "\n" << "FIELDS x y z" << "\n";
    myfile << "SIZE 4 4 4" << "\n";
    myfile << "TYPE F F F" << "\n" << "COUNT 1 1 1"<< "\n";
    myfile << "WIDTH " << std::to_string(normals_out->width) << "\n";
    myfile << "HEIGHT " << std::to_string(normals_out->height) << "\n";
    myfile << "VIEWPOINT 0 0 0 1 0 0 0" << "\n";
    myfile << "POINTS " << std::to_string(normals_out->size()) << "\n";
    myfile << "DATA ascii" << "\n";


    for (int i=0; i < normals_out->size(); i++) {
        const float &normal_x = normals_out->points[i].normal_x;
        const float &normal_y = normals_out->points[i].normal_y;
        const float &normal_z = normals_out->points[i].normal_z;

        myfile << boost::lexical_cast<std::string>(normal_x) << " "
               << boost::lexical_cast<std::string>(normal_y) << " "
               << boost::lexical_cast<std::string>(normal_z) << "\n";
    }
    myfile.close();
}

int main(int argc, char **argv)
{
    pcl::PointCloud<PointType>::Ptr cloud(new pcl::PointCloud<PointType>());

    std::string fileName("../surface_cloud.pcd");
    pcl::io::loadPCDFile(fileName, *cloud);

    Eigen::Vector4f pcaCentroid;
    pcl::compute3DCentroid(*cloud, pcaCentroid);
    Eigen::Matrix3f covariance;
    pcl::computeCovarianceMatrixNormalized(*cloud, pcaCentroid, covariance);
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3f> eigen_solver(covariance, Eigen::ComputeEigenvectors);
    Eigen::Matrix3f eigenVectorsPCA = eigen_solver.eigenvectors();
    const Eigen::Vector3f& eigenValuesPCA = eigen_solver.eigenvalues();
    eigenVectorsPCA.col(2) = eigenVectorsPCA.col(0).cross(eigenVectorsPCA.col(1)); //校正主方向间垂直
    eigenVectorsPCA.col(0) = eigenVectorsPCA.col(1).cross(eigenVectorsPCA.col(2));
    eigenVectorsPCA.col(1) = eigenVectorsPCA.col(2).cross(eigenVectorsPCA.col(0));

    std::cout << "特征值va(3x1):\n" << eigenValuesPCA << std::endl;
    std::cout << "特征向量ve(3x3):\n" << eigenVectorsPCA << std::endl;
    std::cout << "质心点(4x1):\n" << pcaCentroid << std::endl;
    /*
    // 另一种计算点云协方差矩阵特征值和特征向量的方式:通过pcl中的pca接口，如下，这种情况得到的特征向量相似特征向量
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloudPCAprojection (new pcl::PointCloud<pcl::PointXYZ>);
    pcl::PCA<pcl::PointXYZ> pca;
    pca.setInputCloud(cloudSegmented);
    pca.project(*cloudSegmented, *cloudPCAprojection);
    std::cerr << std::endl << "EigenVectors: " << pca.getEigenVectors() << std::endl;//计算特征向量
    std::cerr << std::endl << "EigenValues: " << pca.getEigenValues() << std::endl;//计算特征值
    */
    Eigen::Matrix4f tm = Eigen::Matrix4f::Identity();
    Eigen::Matrix4f tm_inv = Eigen::Matrix4f::Identity();
    tm.block<3, 3>(0, 0) = eigenVectorsPCA.transpose();   //R.
    tm.block<3, 1>(0, 3) = -1.0f * (eigenVectorsPCA.transpose()) *(pcaCentroid.head<3>());//  -R*t
    tm_inv = tm.inverse();

    std::cout << "变换矩阵tm(4x4):\n" << tm << std::endl;
    std::cout << "逆变矩阵tm'(4x4):\n" << tm_inv << std::endl;

    pcl::PointCloud<PointType>::Ptr transformedCloud(new pcl::PointCloud<PointType>);
    pcl::transformPointCloud(*cloud, *transformedCloud, tm);

    PointType min_p1, max_p1;
    Eigen::Vector3f c1, c;
    pcl::getMinMax3D(*transformedCloud, min_p1, max_p1);
    c1 = 0.5f*(min_p1.getVector3fMap() + max_p1.getVector3fMap());

    std::cout << "型心c1(3x1):\n" << c1 << std::endl;

    Eigen::Affine3f tm_inv_aff(tm_inv);
    pcl::transformPoint(c1, c, tm_inv_aff);

    Eigen::Vector3f whd, whd1;
    whd1 = max_p1.getVector3fMap() - min_p1.getVector3fMap();
    whd = whd1;
    float sc1 = (whd1(0) + whd1(1) + whd1(2)) / 3;  //点云平均尺度，用于设置主方向箭头大小

    std::cout << "width1=" << whd1(0) << endl;
    std::cout << "heght1=" << whd1(1) << endl;
    std::cout << "depth1=" << whd1(2) << endl;
    std::cout << "scale1=" << sc1 << endl;

    const Eigen::Quaternionf bboxQ1(Eigen::Quaternionf::Identity());
    const Eigen::Vector3f    bboxT1(c1);

    const Eigen::Quaternionf bboxQ(tm_inv.block<3, 3>(0, 0));
    const Eigen::Vector3f    bboxT(c);


    //变换到原点的点云主方向
    PointType op;
    op.x = 0.0;
    op.y = 0.0;
    op.z = 0.0;
    Eigen::Vector3f px, py, pz;
    Eigen::Affine3f tm_aff(tm);
    pcl::transformVector(eigenVectorsPCA.col(0), px, tm_aff);
    pcl::transformVector(eigenVectorsPCA.col(1), py, tm_aff);
    pcl::transformVector(eigenVectorsPCA.col(2), pz, tm_aff);
    PointType pcaX;
    pcaX.x = sc1 * px(0);
    pcaX.y = sc1 * px(1);
    pcaX.z = sc1 * px(2);
    PointType pcaY;
    pcaY.x = sc1 * py(0);
    pcaY.y = sc1 * py(1);
    pcaY.z = sc1 * py(2);
    PointType pcaZ;
    pcaZ.x = sc1 * pz(0);
    pcaZ.y = sc1 * pz(1);
    pcaZ.z = sc1 * pz(2);

    //初始点云的主方向
    PointType cp;
    cp.x = pcaCentroid(0);
    cp.y = pcaCentroid(1);
    cp.z = pcaCentroid(2);
    PointType pcX;
    pcX.x = sc1 * eigenVectorsPCA(0, 0) + cp.x;
    pcX.y = sc1 * eigenVectorsPCA(1, 0) + cp.y;
    pcX.z = sc1 * eigenVectorsPCA(2, 0) + cp.z;
    PointType pcY;
    pcY.x = sc1 * eigenVectorsPCA(0, 1) + cp.x;
    pcY.y = sc1 * eigenVectorsPCA(1, 1) + cp.y;
    pcY.z = sc1 * eigenVectorsPCA(2, 1) + cp.z;
    PointType pcZ;
    pcZ.x = sc1 * eigenVectorsPCA(0, 2) + cp.x;
    pcZ.y = sc1 * eigenVectorsPCA(1, 2) + cp.y;
    pcZ.z = sc1 * eigenVectorsPCA(2, 2) + cp.z;

    /// **************  划分多个单元格进行表面法线计算  ************** ///

    const float len_x = whd(0); // x轴方向边界框长度
    const float len_y = whd(1); // y轴方向边界框长度
    const float len_z = whd(2); // z轴方向边界框长度

    int point_num_z; // z轴方向放置相机数  (3,5,7---)
    int point_num_y; // y轴方向放置相机数  (3,5,7---)

    if (len_y > len_z) {
        point_num_z = 3;
        point_num_y = 13;
    } else {
        point_num_y = 3;
        point_num_z = 13;
    }

    assert(point_num_z%2 == 1 && point_num_z > 1);
    assert(point_num_y%2 == 1 && point_num_y > 1);

    const float voxel_len_x = len_x; // 单元格的x轴方向长度
    const float voxel_len_y = len_y/(point_num_y-1); // 单元格的y轴方向长度
    const float voxel_len_z = len_z/(point_num_z-1); // 单元格的z轴方向长度

    std::vector<PointType> camera_point_list = {};
    std::vector<PointType> voxel_center_list = {};
    float offset_x = len_x; // 各相机点x轴方向偏移
    for (int it_z = -(point_num_z/2); it_z < point_num_z/2 + 1; it_z++) {
        for (int it_y = -(point_num_y/2); it_y < point_num_y/2 + 1; it_y++) {
            PointType point;
            float step_y = len_y/(point_num_y-1); // y方向点间隔
            float step_z = len_z/(point_num_z-1); // z方向点间隔
            point.x = bboxT1(0) + offset_x;
            point.y = bboxT1(1) + it_y*step_y;
            point.z = bboxT1(2) + it_z*step_z;
            voxel_center_list.push_back(point); // 存储单元格中心点（x方向在相机平面上）

            // 外围相机点偏移
            if (it_z == -point_num_z/2) {point.z -= len_z/4; point.x -= 0;}
            if (it_z ==  point_num_z/2) {point.z += len_z/4; point.x -= 0;}
            if (it_y == -point_num_y/2) {point.y -= len_y/4; point.x -= 0;}
            if (it_y ==  point_num_y/2) {point.y += len_y/4; point.x -= 0;}
            camera_point_list.push_back(point); // 存储相机中心点
        }
    }

//    pcl::visualization::PCLVisualizer viewer_;
//    pcl::visualization::PointCloudColorHandlerCustom<PointType> handler(transformedCloud, 0, 255, 0); //转换到原点的点云相关
//    viewer_.addPointCloud(cloud, handler, "cloud");

    //visualization
    pcl::visualization::PCLVisualizer viewer;
    viewer.setBackgroundColor(0.0,0.0,0.0);

    pcl::visualization::PointCloudColorHandlerCustom<PointType> tc_handler(transformedCloud, 0, 255, 0); //转换到原点的点云相关
    viewer.addPointCloud(transformedCloud, tc_handler, "transformCloud");
    viewer.addCube(bboxT1, bboxQ1, whd1(0), whd1(1), whd1(2), "bbox1");
    viewer.setShapeRenderingProperties(pcl::visualization::PCL_VISUALIZER_REPRESENTATION, pcl::visualization::PCL_VISUALIZER_REPRESENTATION_WIREFRAME, "bbox1");
    viewer.setShapeRenderingProperties(pcl::visualization::PCL_VISUALIZER_COLOR, 0.0, 1.0, 0.0, "bbox1");

    viewer.addArrow(pcaX, op, 1.0, 0.0, 0.0, false, "arrow_X");
    viewer.addArrow(pcaY, op, 0.0, 1.0, 0.0, false, "arrow_Y");
    viewer.addArrow(pcaZ, op, 0.0, 0.0, 1.0, false, "arrow_Z");

    pcl::visualization::PointCloudColorHandlerCustom<PointType> color_handler(cloud, 255, 0, 0);  //输入的初始点云相关
    viewer.addPointCloud(cloud, color_handler, "cloud");
    viewer.addCube(bboxT, bboxQ, whd(0), whd(1), whd(2), "bbox");
    viewer.setShapeRenderingProperties(pcl::visualization::PCL_VISUALIZER_REPRESENTATION, pcl::visualization::PCL_VISUALIZER_REPRESENTATION_WIREFRAME, "bbox");
    viewer.setShapeRenderingProperties(pcl::visualization::PCL_VISUALIZER_COLOR, 1.0, 0.0, 0.0, "bbox");

    viewer.addArrow(pcX, cp, 1.0, 0.0, 0.0, false, "arrow_x");
    viewer.addArrow(pcY, cp, 0.0, 1.0, 0.0, false, "arrow_y");
    viewer.addArrow(pcZ, cp, 0.0, 0.0, 1.0, false, "arrow_z");


    // 画各个视点
    for (int i=0; i < camera_point_list.size(); i++) {
        string pointname = "point" + to_string(i);
        viewer.addSphere(camera_point_list[i], 0.005, pointname);
    }

    // 画各个单元格
    for (int i=0; i < voxel_center_list.size(); i++) {
        string voxelname = "voxel" + to_string(i);
        Eigen::Vector3f center;
        center(0) = voxel_center_list[i].x - offset_x;
        center(1) = voxel_center_list[i].y;
        center(2) = voxel_center_list[i].z;
        viewer.addCube(center, Eigen::Quaternionf::Identity(), len_x, voxel_len_y, voxel_len_z, voxelname);
        viewer.setShapeRenderingProperties(pcl::visualization::PCL_VISUALIZER_REPRESENTATION,
                                           pcl::visualization::PCL_VISUALIZER_REPRESENTATION_WIREFRAME, voxelname);
        viewer.setShapeRenderingProperties(pcl::visualization::PCL_VISUALIZER_COLOR, 1.0, 0.0, 0.0, voxelname);
    }

    // 分割各区域并计算法线
    pcl::PointCloud<pcl::PointNormal>::Ptr cloud_with_normals(new pcl::PointCloud<pcl::PointNormal>);
    pcl::NormalEstimationOMP<PointType, pcl::Normal> estimator(6);
    pcl::search::KdTree<PointType>::Ptr tree_ptr(new pcl::search::KdTree<PointType>);
    for (int i=0; i < camera_point_list.size(); i++) {
        string pointname = "point" + to_string(i);

        pcl::PointCloud<PointType>::Ptr cloud_voxel(new pcl::PointCloud<PointType>);
        for (int j = 0; j < transformedCloud->size(); j++) {
            const PointType &p = transformedCloud->points[j];
            if (p.x >= (voxel_center_list[i].x-offset_x)-voxel_len_x/2 && p.x <= (voxel_center_list[i].x-offset_x)+voxel_len_x/2 &&
                p.y >= voxel_center_list[i].y-voxel_len_y/2 && p.y <= voxel_center_list[i].y+voxel_len_y/2 &&
                p.z >= voxel_center_list[i].z-voxel_len_z/2 && p.z <= voxel_center_list[i].z+voxel_len_z/2) {
                cloud_voxel->push_back(p);
            }
        }

        if (cloud_voxel->empty()) continue; // 跳过无点单元格

        pcl::PointCloud<pcl::Normal>::Ptr cloud_normals_voxel(new pcl::PointCloud<pcl::Normal>);
        estimator.setInputCloud(cloud_voxel);
        estimator.setSearchMethod(tree_ptr);
        estimator.setViewPoint(camera_point_list[i].x, camera_point_list[i].y, camera_point_list[i].z);
        estimator.setRadiusSearch(0.005);
        estimator.compute(*cloud_normals_voxel);

        // 点云与法线融合
        pcl::PointCloud<pcl::PointNormal>::Ptr cloud_with_normals_voxel(new pcl::PointCloud<pcl::PointNormal>);
        pcl::concatenateFields (*cloud_voxel, *cloud_normals_voxel, *cloud_with_normals_voxel);
        *cloud_with_normals += *cloud_with_normals_voxel; // 拼接各区域

        string cloud_filter_name = "cloud_voxel" + to_string(i);
        pcl::visualization::PointCloudColorHandlerCustom<PointType> cloud_filter_handler(cloud_voxel, 0, 0, 255); //转换到原点的点云相关
        viewer.addPointCloud(cloud_voxel, cloud_filter_handler, cloud_filter_name);
        viewer.setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 6, cloud_filter_name);

        string normal_name = "normal_voxel" + to_string(i);
        viewer.addPointCloudNormals<PointType, pcl::Normal>(cloud_voxel, cloud_normals_voxel, 1, 0.01, normal_name);//法线标签
        viewer.setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_COLOR, 1.0, 0.0, 0.0, normal_name);
    }

    // 显示拼接后的点云及法线
    pcl::PointCloud<pcl::Normal>::Ptr normals_out(new pcl::PointCloud<pcl::Normal>);
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_out(new pcl::PointCloud<pcl::PointXYZ>);
    pcl::copyPointCloud(*cloud_with_normals, *normals_out);
    pcl::copyPointCloud(*cloud_with_normals, *cloud_out);

    // 保存拼接后的点云及法线
    pcl::PCDWriter writer;
    writer.writeASCII("../cloud_with_normals.pcd", *cloud_with_normals);
    writer.writeASCII("../cloud.pcd", *cloud_out);
    writer.writeASCII("../normals.pcd", *normals_out);
    save_normals("../normals_as_xyz.pcd", normals_out);

    pcl::visualization::PCLVisualizer viewer_;
    viewer_.addCoordinateSystem(0.1);
    pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ> handler(cloud_out, 0, 0, 255); //转换到原点的点云相关
    viewer_.addPointCloud(cloud_out, handler, "cloud_out");
    viewer_.setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 6, "cloud_out");

    viewer_.addPointCloudNormals<pcl::PointXYZ, pcl::Normal>(cloud_out, normals_out, 1, 0.01, "normals_out"); //法线标签
    viewer_.setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_COLOR, 1.0, 0.0, 0.0, "normals_out");

    viewer.addCoordinateSystem(0.5f*sc1);
    viewer.setBackgroundColor(1.0, 1.0, 1.0);
    while (!viewer.wasStopped() && !viewer_.wasStopped()) {
        viewer.spinOnce(100);
    }

    return 0;
}