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
extern "C" {
        #include <libavformat/avformat.h>
        #include <libavcodec/avcodec.h>
        }

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
        std::cout << "Usage: ./parse_and_tag input.h265|rtmp_url output.csv x output_dir" << std::endl;
        std::cout << "       input.h265: 本地H265文件或RTMP流地址" << std::endl;
        return -1;
    }
    std::string videoPath = argv[1];
    std::string seiCsvPath = argv[2];
    int x = std::stoi(argv[3]);
    std::string outputDir = argv[4];
    boost::filesystem::create_directories(outputDir);

    // 判断是否为RTMP流
    bool isRTMP = (videoPath.find("rtmp://") == 0);

    std::vector<SEIMetadata> seiMetas;
    if (!isRTMP) {
        // 本地文件：直接提取SEI和保存帧
        seiMetas = extractSEIMetasToCSV(videoPath, seiCsvPath);
        cv::VideoCapture cap(videoPath);
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
                    if (saveIndex < seiMetas.size()) {
                        writeExif(filename.str(), seiMetas[saveIndex]);
                    }
                }
                saveIndex++;
            }
            frameIndex++;
        }
        cap.release();
    } else {
        // RTMP流：ffmpeg提取SEI，OpenCV保存帧，顺序对齐
        avformat_network_init();
        AVFormatContext* fmt_ctx = nullptr;
        if (avformat_open_input(&fmt_ctx, videoPath.c_str(), nullptr, nullptr) < 0) {
            std::cerr << "无法打开RTMP流: " << videoPath << std::endl;
            return -1;
        }
        if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
            std::cerr << "无法获取流信息" << std::endl;
            avformat_close_input(&fmt_ctx);
            return -1;
        }
        int video_stream_index = -1;
        for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
            if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                video_stream_index = i;
                break;
            }
        }
        if (video_stream_index == -1) {
            std::cerr << "未找到视频流" << std::endl;
            avformat_close_input(&fmt_ctx);
            return -1;
        }
        AVPacket pkt;
        av_init_packet(&pkt);
        // 先缓存SEI元数据
        std::vector<SEIMetadata> rtmpSeiMetas;
        std::ofstream csvFile(seiCsvPath);
        csvFile << "Latitude,Longitude,Altitude(m),Roll(deg),Pitch(deg),Yaw(deg)\n";
        // 只缓存前N个SEI，N为最大可能帧数
        int maxSei = 100000;
        while (av_read_frame(fmt_ctx, &pkt) >= 0) {
            if (pkt.stream_index == video_stream_index) {
                std::vector<uint8_t> buffer(pkt.data, pkt.data + pkt.size);
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
                            if (ExtractSEIMetadata(nalu, 39, meta, seiCsvPath)) {
                                rtmpSeiMetas.push_back(meta);
                                csvFile << std::fixed << std::setprecision(7)
                                        << meta.latitude << ","
                                        << meta.longitude << ","
                                        << std::setprecision(3) << meta.altitude << ","
                                        << std::setprecision(2) << meta.roll << ","
                                        << meta.pitch << ","
                                        << meta.yaw << "\n";
                                if ((int)rtmpSeiMetas.size() >= maxSei) break;
                            }
                        }
                        pos = next;
                    } else {
                        ++pos;
                    }
                }
            }
            av_packet_unref(&pkt);
            if ((int)rtmpSeiMetas.size() >= maxSei) break;
        }
        csvFile.close();
        avformat_close_input(&fmt_ctx);
        avformat_network_deinit();
        // 用OpenCV保存帧并顺序写EXIF
        cv::VideoCapture cap(videoPath, cv::CAP_FFMPEG);
        if (!cap.isOpened()) {
            std::cerr << "Error: Could not open RTMP stream for frame saving: " << videoPath << std::endl;
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
                    if (saveIndex < (int)rtmpSeiMetas.size()) {
                        writeExif(filename.str(), rtmpSeiMetas[saveIndex]);
                    }
                }
                saveIndex++;
            }
            frameIndex++;
        }
        cap.release();
    }
    return 0;
}
