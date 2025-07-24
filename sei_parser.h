#ifndef SEI_PARSER_H

#define SEI_PARSER_H

#include <vector>
#include <cstdint>
#include <string>

void ParseSEI(std::vector<uint8_t>& nalu, uint8_t naluType);

struct SEIMetadata {
    double latitude = 0;
    double longitude = 0;
    double altitude = 0;
    float roll = 0;
    float pitch = 0;
    float yaw = 0;
    bool valid = false;
};

// 新增：带csv路径的元数据提取
bool ExtractSEIMetadata(std::vector<uint8_t>& nalu, uint8_t naluType, SEIMetadata& meta, const std::string& csvFilePath);

// bool ExtractSEIMetadata(std::vector<uint8_t>& nalu, uint8_t naluType, SEIMetadata& meta);
bool isSEINalu(uint8_t nal_unit_type, bool isH265);

#endif
