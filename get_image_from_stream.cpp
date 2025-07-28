
#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <iomanip>
#include <vector>
#include <boost/filesystem.hpp>
#include "sei_parser.h"
#include <exiv2/exiv2.hpp>

// 声明写EXIF函数（可直接复用extract_and_tag.cpp中的实现）
void writeExif(const std::string& path, const SEIMetadata& meta) {
    Exiv2::Image::AutoPtr image = Exiv2::ImageFactory::open(path);
    image->readMetadata();
    Exiv2::ExifData& exifData = image->exifData();
    char latRef = 'N', lonRef = 'E';
    auto toExifGpsCoordinate = [](double value, char& ref) {
        ref = (value < 0) ? ((ref == 'N') ? 'S' : 'W') : ((ref == 'N') ? 'N' : 'E');
        value = std::abs(value);
        int degrees = static_cast<int>(value);
        double minutesFraction = (value - degrees) * 60.0;
        int minutes = static_cast<int>(minutesFraction);
        double seconds = (minutesFraction - minutes) * 60.0;
        seconds = std::round(seconds * 1000.0) / 1000.0;
        std::ostringstream oss;
        oss << degrees << "/1 " << minutes << "/1 " << static_cast<int>(seconds * 1000) << "/1000";
        return oss.str();
    };
    exifData["Exif.GPSInfo.GPSLatitude"] = toExifGpsCoordinate(meta.latitude, latRef);
    exifData["Exif.GPSInfo.GPSLatitudeRef"] = std::string(1, latRef);
    exifData["Exif.GPSInfo.GPSLongitude"] = toExifGpsCoordinate(meta.longitude, lonRef);
    exifData["Exif.GPSInfo.GPSLongitudeRef"] = std::string(1, lonRef);
    exifData["Exif.GPSInfo.GPSAltitude"] = Exiv2::Rational(static_cast<int>(meta.altitude * 1000), 1000);
    exifData["Exif.GPSInfo.GPSAltitudeRef"] = meta.altitude < 0 ? 1 : 0;
    Exiv2::XmpProperties::registerNs("http://example.com/PoseDegree", "PoseDegree");
    Exiv2::XmpData& xmpData = image->xmpData();
    xmpData["Xmp.PoseDegree.roll"]  = std::to_string(meta.roll);
    xmpData["Xmp.PoseDegree.pitch"] = std::to_string(meta.pitch);
    xmpData["Xmp.PoseDegree.yaw"]   = std::to_string(meta.yaw);
    image->setExifData(exifData);
    image->setXmpData(xmpData);
    image->writeMetadata();
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cout << "Usage: ./decode_h265_save input.h265|rtmp_url sei_meta.csv x [output_dir]" << std::endl;
        std::cout << "支持本地文件或rtmp流作为输入" << std::endl;
        return -1;
    }

    std::string videoPath = argv[1];
    std::string seiCsvPath = argv[2];
    int x = std::stoi(argv[3]);
    std::string outputDir = (argc >= 5) ? argv[4] : "output_images";
    boost::filesystem::create_directories(outputDir);

    // 读取SEI元数据CSV到vector<SEIMetadata>
    std::vector<SEIMetadata> seiMetas;
    std::ifstream csv(seiCsvPath);
    std::string line;
    std::getline(csv, line); // 跳过表头
    while (std::getline(csv, line)) {
        std::istringstream iss(line);
        std::string token;
        SEIMetadata meta;
        std::getline(iss, token, ','); meta.latitude = std::stod(token);
        std::getline(iss, token, ','); meta.longitude = std::stod(token);
        std::getline(iss, token, ','); meta.altitude = std::stod(token);
        std::getline(iss, token, ','); meta.roll = std::stof(token);
        std::getline(iss, token, ','); meta.pitch = std::stof(token);
        std::getline(iss, token, ','); meta.yaw = std::stof(token);
        meta.valid = true;
        seiMetas.push_back(meta);
    }

    // 判断输入是本地文件还是rtmp流
    bool isRtmp = (videoPath.find("rtmp://") == 0);
    cv::VideoCapture cap;
    if (isRtmp) {
        std::cout << "打开RTMP流: " << videoPath << std::endl;
        cap.open(videoPath, cv::CAP_FFMPEG);
    } else {
        cap.open(videoPath);
    }
    if (!cap.isOpened()) {
        std::cerr << "Error: Could not open video: " << videoPath << std::endl;
        return -1;
    }

    int frameIndex = 0, saveIndex = 0;
    cv::Mat frame;
    while (true) {
        bool ret = cap.read(frame);
        if (!ret) break;
        if (frameIndex % x == 0) {
            std::ostringstream filename;
            filename << outputDir << "/frame_" << std::setw(4) << std::setfill('0') << saveIndex << ".jpg";
            bool saveOk = cv::imwrite(filename.str(), frame);
            if (!saveOk) {
                std::cerr << "Failed to save image: " << filename.str() << std::endl;
            } else {
                std::cout << "Saved: " << filename.str() << std::endl;
                // 写入EXIF元数据
                if (saveIndex < seiMetas.size()) {
                    writeExif(filename.str(), seiMetas[saveIndex]);
                }
            }
            saveIndex++;
        }
        frameIndex++;
    }
    cap.release();
    return 0;
}
