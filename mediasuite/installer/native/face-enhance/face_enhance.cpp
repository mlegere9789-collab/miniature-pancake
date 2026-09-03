// MediaSuite's own CLI driver for the vendored GFPGAN-ncnn face-restoration classes
// (face.h/face.cpp, gfpgan.h/gfpgan.cpp — see LICENSE-THIRD-PARTY.txt) rather than a fake
// standing in for one. Unlike the original demo/main.cpp this is based on, this:
//   - takes real arguments (-i input -o output -m models-dir) instead of a hardcoded
//     "./models" path and a single positional argument, so it fits the same
//     RunToolAsync-driven convention every other bundled tool in this app follows;
//   - never opens a window (no cv::imshow/cv::waitKey) — this runs on a background job
//     thread, often on a CI-class machine with no display at all;
//   - always writes the output file and exits 0 when no face is found, copying the input
//     through unchanged rather than failing the whole upscale job over a photo with
//     nobody in it;
//   - only restores detected faces and pastes them back onto the image handed in, never
//     upscaling the background itself — MediaSuite's own UpscaleEngine already produced
//     that upscaled background through Real-ESRGAN before this ever runs, so this tool
//     does not need (and does not bundle) GFPGAN-ncnn's own separate real_esrgan model.
#include <opencv2/opencv.hpp>
#include "face.h"
#include "gfpgan.h"

#include <cstdio>
#include <string>

namespace
{
    // Ported as-is from the reference GFPGAN-ncnn demo (same BSD-3-Clause source tree —
    // see LICENSE-THIRD-PARTY.txt): converts GFPGAN's raw ncnn output tensor, in its
    // [-1, 1] float range and BGR-reversed channel order, into an ordinary 8-bit OpenCV
    // image.
    void ToOpenCv(const ncnn::Mat& result, cv::Mat& out)
    {
        cv::Mat float32 = cv::Mat::zeros(cv::Size(512, 512), CV_32FC3);
        for (int y = 0; y < result.h; y++)
        {
            for (int x = 0; x < result.w; x++)
            {
                float32.at<cv::Vec3f>(y, x)[2] = (result.channel(0)[y * result.w + x] + 1) / 2;
                float32.at<cv::Vec3f>(y, x)[1] = (result.channel(1)[y * result.w + x] + 1) / 2;
                float32.at<cv::Vec3f>(y, x)[0] = (result.channel(2)[y * result.w + x] + 1) / 2;
            }
        }

        float32.convertTo(out, CV_8UC3, 255.0, 0);
    }

    // Ported as-is from the reference GFPGAN-ncnn demo: warps a restored 512x512 face
    // back into its original position and blends it into the background image with a
    // soft-edged mask, so the swap does not leave a hard rectangle behind.
    void PasteFaceOntoImage(const cv::Mat& restoredFace, cv::Mat& inverseTransform, cv::Mat& background)
    {
        inverseTransform.at<float>(0, 2) += 1.0;
        inverseTransform.at<float>(1, 2) += 1.0;

        cv::Mat invRestored;
        cv::warpAffine(restoredFace, invRestored, inverseTransform, background.size(), 1, 0);

        cv::Mat mask = cv::Mat::ones(cv::Size(512, 512), CV_8UC1) * 255;
        cv::Mat invMask;
        cv::warpAffine(mask, invMask, inverseTransform, background.size(), 1, 0);

        cv::Mat invMaskEroded;
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(4, 4));
        cv::erode(invMask, invMaskEroded, kernel);

        cv::Mat pastedFace;
        cv::bitwise_and(invRestored, invRestored, pastedFace, invMaskEroded);

        int faceArea = cv::countNonZero(invMaskEroded);
        int edgeWidth = static_cast<int>(std::sqrt(static_cast<double>(faceArea)) / 20);
        int erosionRadius = edgeWidth * 2;
        cv::Mat invMaskCenter;
        kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(erosionRadius, erosionRadius));
        cv::erode(invMaskEroded, invMaskCenter, kernel);

        int blurSize = edgeWidth * 2;
        cv::Mat softMask;
        cv::GaussianBlur(invMaskCenter, softMask, cv::Size(blurSize + 1, blurSize + 1), 0, 0, 4);

        for (int y = 0; y < background.rows; y++)
        {
            for (int x = 0; x < background.cols; x++)
            {
                double alpha = softMask.at<uchar>(y, x) / 255.0;
                for (int channel = 0; channel < 3; channel++)
                {
                    background.at<cv::Vec3b>(y, x)[channel] = static_cast<uchar>(
                        pastedFace.at<cv::Vec3b>(y, x)[channel] * alpha
                        + (1 - alpha) * background.at<cv::Vec3b>(y, x)[channel]);
                }
            }
        }
    }

    bool ParseArguments(int argc, char** argv, std::string& inputPath, std::string& outputPath, std::string& modelsDir)
    {
        for (int i = 1; i < argc - 1; i++)
        {
            std::string flag = argv[i];
            if (flag == "-i")
            {
                inputPath = argv[++i];
            }
            else if (flag == "-o")
            {
                outputPath = argv[++i];
            }
            else if (flag == "-m")
            {
                modelsDir = argv[++i];
            }
        }

        return !inputPath.empty() && !outputPath.empty() && !modelsDir.empty();
    }
}

int main(int argc, char** argv)
{
    std::string inputPath, outputPath, modelsDir;
    if (!ParseArguments(argc, argv, inputPath, outputPath, modelsDir))
    {
        std::fprintf(stderr, "Usage: face_enhance -i <input> -o <output> -m <models-dir>\n");
        return 1;
    }

    cv::Mat image = cv::imread(inputPath, cv::IMREAD_COLOR);
    if (image.empty())
    {
        std::fprintf(stderr, "Could not read '%s' as an image.\n", inputPath.c_str());
        return 1;
    }

    Face faceDetector;
    if (faceDetector.load(modelsDir + "/yolov5-blazeface.param", modelsDir + "/yolov5-blazeface.bin") != 0)
    {
        std::fprintf(stderr, "Could not load the face-detector model from '%s'.\n", modelsDir.c_str());
        return 1;
    }

    GFPGAN gfpgan;
    if (gfpgan.load(modelsDir + "/encoder.param", modelsDir + "/encoder.bin", modelsDir + "/style.bin") != 0)
    {
        std::fprintf(stderr, "Could not load the GFPGAN model from '%s'.\n", modelsDir.c_str());
        return 1;
    }

    std::vector<Object> faces;
    faceDetector.detect(image, faces, 0.35f);

    if (faces.empty())
    {
        // No face to restore is not a failure — the job's own upscaled output is already
        // a perfectly good result, so it is written straight through unchanged.
        if (!cv::imwrite(outputPath, image))
        {
            std::fprintf(stderr, "Could not write '%s'.\n", outputPath.c_str());
            return 1;
        }

        std::printf("No faces detected; wrote the input through unchanged.\n");
        return 0;
    }

    std::vector<cv::Mat> inverseTransforms, alignedFaces;
    faceDetector.align_warp_face(image, faces, inverseTransforms, alignedFaces);

    cv::Mat result = image.clone();
    for (std::size_t i = 0; i < faces.size(); i++)
    {
        ncnn::Mat gfpganOutput;
        gfpgan.process(alignedFaces[i], gfpganOutput);

        cv::Mat restoredFace;
        ToOpenCv(gfpganOutput, restoredFace);

        PasteFaceOntoImage(restoredFace, inverseTransforms[i], result);
    }

    if (!cv::imwrite(outputPath, result))
    {
        std::fprintf(stderr, "Could not write '%s'.\n", outputPath.c_str());
        return 1;
    }

    std::printf("Restored %zu face(s).\n", faces.size());
    return 0;
}
