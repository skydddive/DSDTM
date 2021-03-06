//
// Created by buyi on 17-10-16.
//

#include "Frame.h"

namespace DSDTM
{
unsigned long Frame::mlNextId = 0;

Frame::Frame()
{

}
// Copy contructor
Frame::Frame(Frame &frame):
        mCamera(frame.mCamera), mlId(frame.mlId), mdCloTimestamp(frame.mdCloTimestamp),
        mT_c2w(frame.mT_c2w), mColorImg(frame.mColorImg), mvImg_Pyr(frame.mvImg_Pyr),
        mDepthImg(frame.mDepthImg), mvFeatures(frame.mvFeatures), mPyra_levels(frame.mPyra_levels),
        mvGrid_probability(frame.mvGrid_probability), mvMapPoints(frame.mvMapPoints),
        mpObservation(frame.mpObservation)
{

}

// construct monocular frame
Frame::Frame(CameraPtr _cam, cv::Mat _colorIag, double _timestamp):
        mCamera(_cam), mColorImg(_colorIag), mdCloTimestamp(_timestamp)
{
    mlId = mlNextId++;
    InitFrame();
}

// construct rgbd frame
Frame::Frame(CameraPtr _cam, cv::Mat _colorImg, cv::Mat _depthImg, double _ctimestamp):
        mCamera(_cam), mColorImg(_colorImg), mdCloTimestamp(_ctimestamp),
        mDepthImg(_depthImg)
{
    mlId = mlNextId++;
    InitFrame();
}

Frame::~Frame()
{

}

void Frame::InitFrame()
{
    ResetFrame();
    mPyra_levels = Config::Get<int>("Camera.MaxPyraLevels");
    mMin_Dist = Config::Get<int>("Camera.Min_dist");

    mvImg_Pyr.resize(mPyra_levels);
    ComputeImagePyramid(mColorImg, mvImg_Pyr);

    //! it takes almost 6ms to undistort the image, it takes too much time

    //cv::Mat tColorImg(mColorImg);
    //mColorImg.release();
    //cv::undistort(tColorImg, mColorImg, mCamera->mInstrinsicMat, mCamera->mDistortionMat, mCamera->mInstrinsicMat);


    mImgMask = cv::Mat(mCamera->mheight, mCamera->mwidth, CV_8UC1, cv::Scalar(255));
    mDynamicMask = cv::Mat(mCamera->mheight, mCamera->mwidth, CV_8UC1, cv::Scalar(0));
}

void Frame::ResetFrame()
{
    mvFeatures.clear();
    mvGrid_probability.clear();
}

void Frame::ComputeImagePyramid(const cv::Mat Image, std::vector<cv::Mat> &Img_Pyr)
{
    Img_Pyr[0] = Image;
    for (int i = 1; i < mPyra_levels; ++i)
    {
        cv::pyrDown(Img_Pyr[i-1], Img_Pyr[i]);
    }
}

void Frame::Add_Feature(Feature *tfeature, bool tbNormal)
{
    if(tbNormal)
    {
        tfeature->mNormal = mCamera->Pixel2Camera(tfeature->mpx, 1.0);
        tfeature->mNormal.normalize();
    }

    mvFeatures.push_back(tfeature);
}

void Frame::UndistortFeatures()
{
    int N = mvFeatures.size();
    mvMapPoints.resize(N);

    std::vector<cv::Point2f> tSrc;
    std::vector<cv::Point2f> tmvFeaturesUn;           //Features undistorted

    int tNum = 0;
    for (auto it = mvFeatures.begin(); it!=mvFeatures.end(); ++it)
    {
        //if((*it)->mbInitial)
        //    continue;

        tSrc.push_back((*it)->mpx);
        tNum++;
    }

    cv::Mat mat(tNum, 2, CV_32F);
    for (int k = 0; k < tNum; ++k)
    {
        mat.at<float>(k, 0)=tSrc[k].x;
        mat.at<float>(k, 1)=tSrc[k].y;
    }

    //! the InputArray in undistortPoints should be 2 channels
    cv::Mat tDistortionMat(mCamera->mDistortionMat.size(), CV_32F, cv::Scalar(0));

    mat=mat.reshape(2);
    cv::undistortPoints(mat, mat, mCamera->mInstrinsicMat,
                        mCamera->mDistortionMat, cv::noArray(), mCamera->mInstrinsicMat);
    mat=mat.reshape(1);

    /*
    int i = 0;
    for (int j = N - tNum; j < N; ++j, ++i)
    {
        mvFeatures[j]->mpx.x = mat.at<float>(i, 0);
        mvFeatures[j]->mpx.y = mat.at<float>(i, 1);

        mvFeatures[j]->mNormal = mCamera->Pixel2Camera(mvFeatures[j]->mpx, 1.0);
        mvFeatures[j]->mNormal.normalize();
    }
     */

    for (int j = 0; j < tNum; ++j)
    {
        if(mvFeatures[j]->mbInitial)
            continue;

        mvFeatures[j]->mpx.x = mat.at<float>(j, 0);
        mvFeatures[j]->mpx.y = mat.at<float>(j, 1);

        mvFeatures[j]->mNormal = mCamera->Pixel2Camera(mvFeatures[j]->mpx, 1.0);
        mvFeatures[j]->mNormal.normalize();
    }
}

Eigen::Vector3d Frame::UnProject(const cv::Point2f tPixel, const float d)
{
    Eigen::Vector3d tPoint = mCamera->Pixel2Camera(tPixel, d);

    return mT_c2w.inverse()*tPoint;
}

void Frame::Get_Features(std::vector<cv::Point2f> &_features)
{
    for (int i = 0; i < mvFeatures.size(); ++i)
    {
        _features.push_back(mvFeatures[i]->mpx);
    }
}

void Frame::Set_Pose(Sophus::SE3 _pose)
{
    mT_c2w = _pose;

    Sophus::SE3 tT_w2c = mT_c2w.inverse();
    mR_w2c = tT_w2c.so3();
    mOw = tT_w2c.translation();
}

float Frame::Get_FeatureDetph(const Feature* feature)
{
    int x = cvRound(feature->mpx.x);
    int y = cvRound(feature->mpx.y);

    float d = mDepthImg.ptr<float>(y)[x];
    if(d != 0)
        return d;
    else
    {
        int dx[4] = {-1, 0, 1, 0};
        int dy[4] = {0, -1, 0, 1};
        for (int i = 0; i < 4; ++i)
        {
            d = mDepthImg.ptr<float>(y+dy[i])[x+dx[i]];
            if(d != 0)
            {
                return d;
            }
        }
    }

    return -1.0;
}

float Frame::Get_FeatureDetph(const cv::Point2f feature)
{
    int x = cvRound(feature.x);
    int y = cvRound(feature.y);

    float d = mDepthImg.ptr<float>(y)[x];
    if(d != 0)
        return d;
    else
    {
        int dx[4] = {-1, 0, 1, 0};
        int dy[4] = {0, -1, 0, 1};
        for (int i = 0; i < 4; ++i)
        {
            d = mDepthImg.ptr<float>(y+dy[i])[x+dx[i]];
            if(d != 0)
            {
                return d;
            }
        }
    }

    return -1.0;
}

//! Add MapPoint to do BA optimize frame pose
void Frame::Add_MapPoint(MapPoint *tMPoint, int tIdx)
{
    mvMapPoints[tIdx] = tMPoint;
}

void Frame::Add_MapPoint(MapPoint *tMPoint)
{
    mvMapPoints.push_back(tMPoint);
}

void Frame::Add_Observations(const KeyFrame &tKframe)
{

}


bool Frame::Get_SceneDepth(double &tMinDepth, double &tMeanDepth)
{
    mMinDepth = std::numeric_limits<double>::max();
    std::vector<double> tDepth_vec;

    for (auto it = this->mvFeatures.begin(); it != this->mvFeatures.end(); it++)
    {
        if(!(*it)->Mpt)
            continue;

        if((*it)->Mpt->IsBad())
            continue;

        Eigen::Vector3d tPose = mT_c2w*(*it)->Mpt->Get_Pose();
        const double z = tPose(2);

        tDepth_vec.push_back(z);
        mMinDepth = fmin(mMinDepth, z);
    }

    if (tDepth_vec.empty())
    {
        LOG(ERROR)<< "The scene depth is wrong in frame: %d" << this->mlId;
        return false;
    }

    mMeanDepth = utils::GetMedian(tDepth_vec);

    tMinDepth = mMinDepth;
    tMeanDepth = mMeanDepth;

    return true;
}

bool Frame::Find_Observations(size_t tID)
{
    if(mpObservation.count(tID) > 0)
        return true;
    else
        return false;
}


void Frame::Set_Mask()
{
    for (int k = 0; k < mvFeatures.size(); ++k)
    {
        if(mvMapPoints[k])
            cv::circle(mImgMask, mvFeatures[k]->mpx, mMin_Dist, 0, -1);
    }

    cv::threshold(mDynamicMask, mDynamicMask, 200, 255, CV_THRESH_BINARY);

    mImgMask = mImgMask - mDynamicMask;
    std::cout <<" ";
}

bool Frame::isVisible(const Eigen::Vector3d tPose, int tBoundary) const
{
    Eigen::Vector3d tPoseCam = mT_c2w*tPose;
    if(tPoseCam(2) < 0.0)
        return false;

    Eigen::Vector2d px = mCamera->Camera2Pixel(tPoseCam);
    if(mCamera->IsInImage(cv::Point2f(px(0), px(1)), tBoundary))
        return true;

    return false;
}

void Frame::Reset_Gridproba()
{
    mvGrid_probability.resize(100, 0.5);
}

Eigen::Vector2d Frame::World2Pixel(const Eigen::Vector3d &Point)
{
    Eigen::Vector3d tPoint = mT_c2w*Point;

    return mCamera->Camera2Pixel(tPoint);
}

void Frame::Get_CorrespondMp()
{

}

int Frame::Get_VaildMpNums()
{
    int N = 0;
    for(const auto &it : mvMapPoints)
    {
        if(it)
            N++;
    }

    return N;
}

void Frame::Motion_Removal(const cv::Mat tMask)
{
    mDynamicMask = tMask;

    Features tmpFeatures = mvFeatures;
    std::vector<MapPoint*> tmpMps = mvMapPoints;

    int j = 0;
    for (int i = 0; i < mvFeatures.size(); ++i)
    {
        if(tMask.at<uchar>(cv::Point2f(mvFeatures[i]->mpx.x, mvFeatures[i]->mpx.y))!=255)
        {
            tmpFeatures.push_back(mvFeatures[i]);
            tmpMps.push_back(mvMapPoints[i]);
        }
    }
    mvFeatures.clear();
    mvMapPoints.clear();

    mvFeatures = tmpFeatures;
    mvMapPoints = tmpMps;
}

bool Frame::IsinFrustum(MapPoint *tMp, float tfViewCosLimit)
{
    Eigen::Vector3d tPoseW = tMp->Get_Pose();

    Eigen::Vector3d tPoseC = mT_c2w*tPoseW;
    if(tPoseC[2] < 0)
        return false;

    Eigen::Vector2d tPx = mCamera->Camera2Pixel(tPoseC);
    if(!mCamera->IsInImage(cv::Point2f(tPx[0], tPx[1])))
        return false;

    Eigen::Vector3d tVec_C2P = tPoseW - mOw;

    float tDist = tVec_C2P.norm();
    float tMaxDistance = tMp->Get_MaxObserveDistance();
    float tMinDistance = tMp->Get_MinObserveDistance();
    if(tDist > tMaxDistance || tDist < tMinDistance)
        return false;

    Eigen::Vector3d tNormalVec = tMp->Get_NormalVector();
    const float tViewDiff = tVec_C2P.dot(tNormalVec);
    if(tViewDiff < tfViewCosLimit)
        return false;

    return true;
}


}// namespace DSDTM