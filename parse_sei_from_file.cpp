#include <fstream>
#include <vector>
#include <cstdint>
#include <iostream>
#include "sei_parser.h"

// 辅助函数：根据.h265文件名生成.csv文件名
#include <boost/filesystem.hpp>
std::string getCsvFileName(const std::string& h265FilePath) {
    boost::filesystem::path inputPath(h265FilePath);
    std::string fileNameNoExt = inputPath.stem().string(); // 获取不含扩展名的文件名

    // 指定输出目录
    boost::filesystem::path outputDir("/home/nighthe/video_output/pos_file");

    // 构造新的完整输出路径
    boost::filesystem::path outputPath = outputDir / (fileNameNoExt + ".csv");

    return outputPath.string();  // 返回完整路径字符串
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cout << "Usage: ./parse_sei_from_file input.h265|rtmp_url" << std::endl;
        return -1;
    }
    std::string inputPath = argv[1];
    bool isRTMP = (inputPath.find("rtmp://") == 0);
    if (isRTMP) {
        std::cout << "警告: RTMP流暂不支持SEI元数据解析，仅支持本地文件。" << std::endl;
        return 0;
    }
    std::string csvFilePath = getCsvFileName(inputPath);
    std::ifstream file(inputPath, std::ios::binary);
    if (!file)
    {
        std::cerr << "无法打开文件: " << inputPath << std::endl;
        return -1;
    }

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), {});
    file.close();

    size_t pos = 0;
    printf("开始解析 SEI NALU...\n");
    while (pos + 4 < buffer.size())
    {
        // 查找 start code
        if (buffer[pos] == 0x00 && buffer[pos + 1] == 0x00 &&
            ((buffer[pos + 2] == 0x00 && buffer[pos + 3] == 0x01) || buffer[pos + 2] == 0x01))
        {
            size_t start = (buffer[pos + 2] == 0x01) ? pos + 3 : pos + 4;
            size_t next = start;
            // 查找下一个 start code
            while (next + 4 < buffer.size())
            {
                if (buffer[next] == 0x00 && buffer[next + 1] == 0x00 &&
                    ((buffer[next + 2] == 0x00 && buffer[next + 3] == 0x01) || buffer[next + 2] == 0x01))
                    break;
                ++next;
            }
            if (start < next)
            {
                uint8_t nal_unit_type = (buffer[start] & 0x7E) >> 1; // H265
                if (isSEINalu(nal_unit_type, true))
                {
                    std::vector<uint8_t> nalu(buffer.begin() + pos, buffer.begin() + next);
                    // 解析并写入CSV
                    SEIMetadata meta;
                    ExtractSEIMetadata(nalu, 39, meta, csvFilePath);
                }
            }
            pos = next;
        }
        else
        {
            ++pos;
        }
    }
    return 0;
}