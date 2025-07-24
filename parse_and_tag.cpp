#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <iomanip>
#include <vector>
#include <fstream>
#include <sstream>
#include <boost/filesystem.hpp>
#include "sei_parser.h"
#include <exiv2/exiv2.hpp>

// 解析SEI NALU并写入CSV，返回vector<SEIMetadata>
std::vector<SEIMetadata> extractSEIMetasToCSV(const std::string& h265Path, const std::string& csvPath) {
    std::ifstream file(h265Path, std::ios::binary);
    std::vector<SEIMetadata> seiMetas;
    if (!file) return seiMetas;
    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), {});
    file.close();
    std::ofstream csvFile(csvPath);
    csvFile << "Latitude,Longitude,Altitude(m),Roll(deg),Pitch(deg),Yaw(deg)\n";
    size_t pos = 0;
    while (pos + 4 < buffer.size()) {
        if (buffer[pos] == 0x00 && buffer[pos + 1] == 0x00 &&
            ((buffer[pos + 2] == 0x00 && buffer[pos + 3] == 0x01) || buffer[pos + 2] == 0x01)) {
            size_t start = (buffer[pos + 2] == 0x01) ? pos + 3 : pos + 4;
            size_t next = start;
            while (next + 4 < buffer.size()) {
                if (buffer[next] == 0x00 && buffer[next + 1] == 0x00 &&
                    ((buffer[next + 2] == 0x00 && buffer[next + 3] == 0x01) || buffer[next + 2] == 0x01))
                    break;
                ++next;
            }
            uint8_t naluType = (buffer[start] & 0x7E) >> 1;
            if (isSEINalu(naluType, true)) {
                std::vector<uint8_t> nalu(buffer.begin() + pos, buffer.begin() + next);
                SEIMetadata meta;
                if (ExtractSEIMetadata(nalu, 39, meta, csvPath)) {
                    seiMetas.push_back(meta);
                    csvFile << std::fixed << std::setprecision(7)
                            << meta.latitude << ","
                            << meta.longitude << ","
                            << std::setprecision(3) << meta.altitude << ","
                            << std::setprecision(2) << meta.roll << ","
                            << meta.pitch << ","
                            << meta.yaw << "\n";
                }
            }
            pos = next;
        } else {
            ++pos;
        }
    }
    csvFile.close();
    return seiMetas;
}

// 写EXIF元数据
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
    if (argc < 5) {
        std::cout << "Usage: ./parse_and_tag input.h265 output.csv x output_dir" << std::endl;
        return -1;
    }
    std::string videoPath = argv[1];
    std::string seiCsvPath = argv[2];
    int x = std::stoi(argv[3]);
    std::string outputDir = argv[4];
    boost::filesystem::create_directories(outputDir);

    // 1. 提取SEI元数据到vector和CSV
    std::vector<SEIMetadata> seiMetas = extractSEIMetasToCSV(videoPath, seiCsvPath);

    // 2. 逐帧读取视频并保存图片+EXIF
    cv::VideoCapture cap(videoPath);
    if (!cap.isOpened()) {
        std::cerr << "Error: Could not open video file: " << videoPath << std::endl;
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
