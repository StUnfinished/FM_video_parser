#include "sei_parser.h"
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <string>

#ifdef __GNUC__
#define SINGLE_BYTE_PACKED( __Declaration__ ) __Declaration__ __attribute__((packed))
#else
#define SINGLE_BYTE_PACKED( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop) )
#endif

// 旧格式
SINGLE_BYTE_PACKED(
typedef struct SdSei_t
{
    unsigned char head[2];
    struct
    {
        unsigned char rng_trig : 1;
        unsigned char pipState : 3;
        unsigned char reserved : 4;
    } flag;
    int uav_lon;
    int uav_lat;
    int uav_alt;
    int uav_hgt;
    short uav_phi;
    short uav_the;
    unsigned short uav_psi;
    short cam_phi;
    short cam_the;
    unsigned short cam_psi;
    unsigned short cam1_zoom;
    unsigned short cam2_zoom;
    unsigned short rng_dist;
    unsigned short gps_week;
    unsigned int gps_itow;
    int tgt_lon;
    int tgt_lat;
    int tgt_alt;
    unsigned short cam1_fl1x;
    unsigned short cam2_fl1x;
    unsigned char reserved[4];
    unsigned char check_sum;
}) SdSei;

// 新格式（带版本、长度等）
SINGLE_BYTE_PACKED(
typedef struct FmSdSei_t
{
    unsigned char head[2];
    unsigned short len;
    unsigned char ver;
    struct
    {
        unsigned char rng_trig : 1;
        unsigned char pipState : 3;
        unsigned char reserved : 4;
    } flag;
    int uav_lon;
    int uav_lat;
    int uav_alt;
    int uav_egt;
    int uav_hgt;
    short uav_phi;
    short uav_the;
    unsigned short uav_psi;
    short cam_phi;
    short cam_the;
    unsigned short cam_psi;
    unsigned short cam1_zoom;
    unsigned short cam2_zoom;
    unsigned short rng_dist;
    unsigned short gps_week;
    unsigned int gps_itow;
    int tgt_lon;
    int tgt_lat;
    int tgt_alt;
    unsigned short cam1_fl1x;
    unsigned short cam2_fl1x;
    unsigned char check_sum;
}) FmSdSei;

bool isSEINalu(uint8_t nal_unit_type, bool isH265)
{
    return isH265 ? ((nal_unit_type & 0x3F) == 39) : ((nal_unit_type & 0x1F) == 6);
}

// 解码原始 SEI 数据并进行解码器头、UUID、checksum 剥离
static std::pair<std::vector<uint8_t>, bool> _GetSEIRawData(std::vector<uint8_t>& nalu, uint8_t naluType)
{
    size_t naluSize = nalu.size();
    std::vector<uint8_t> buf;
    size_t i = 0;

    for (; i < naluSize - 2; ++i)
    {
        if (nalu[i] == 0 && nalu[i + 1] == 0 && nalu[i + 2] == 3)
        {
            buf.push_back(0);
            buf.push_back(0);
            i += 2;
        }
        else
        {
            buf.push_back(nalu[i]);
        }
    }
    buf.insert(buf.end(), nalu.begin() + i, nalu.end());

    // 去除 NALU 头
    if (buf[2] == 1)
        buf.erase(buf.begin(), buf.begin() + 3);
    else
        buf.erase(buf.begin(), buf.begin() + 4);

    // 去除 payloadType, payloadSize（H264 是 3 字节，H265 是 4 字节）
    if (naluType == 6)        // H.264
        buf.erase(buf.begin(), buf.begin() + 3);
    else if (naluType == 39)  // H.265
        buf.erase(buf.begin(), buf.begin() + 4);

    // 去除 UUID
    if (buf.size() > 75)
        buf.erase(buf.begin(), buf.begin() + 16);

    bool ret = false;
    int sumLen = 0;
    uint8_t sum = 0;

    if (buf.size() > 63 && buf[0] == 0xEE && buf[1] == 0x16)  // Old format
    {
        sumLen = 63;
    }
    else if (buf.size() > 66 && buf[0] == 0xFE && buf[1] == 0x01)  // New format
    {
        int msgLen = (buf[3] << 8) + buf[2];
        if (msgLen > buf.size()) msgLen = buf.size();
        sumLen = msgLen - 1;
    }
    else
    {
        return std::make_pair(buf, false);
    }

    for (int i = 0; i < sumLen; ++i)
        sum += buf[i];

    if (sum == buf[sumLen])
        ret = true;

    return std::make_pair(buf, ret);
}

// 将 SEI 元数据追加到 CSV 文件
void AppendSEIMetadataToCSV(const SEIMetadata& meta, const std::string& csvFilePath)
{
    if (!meta.valid)
        return;

    std::ofstream csvFile;
    csvFile.open(csvFilePath, std::ios::out | std::ios::app);
    if (!csvFile.is_open())
    {
        fprintf(stderr, "Failed to open CSV file: %s\n", csvFilePath.c_str());
        return;
    }

    csvFile.seekp(0, std::ios::end);
    if (csvFile.tellp() == 0)
    {
        csvFile << "Latitude,Longitude,Altitude(m),Roll(deg),Pitch(deg),Yaw(deg)\n";
    }

    csvFile << std::fixed << std::setprecision(7)
            << meta.latitude << ","
            << meta.longitude << ","
            << std::setprecision(3) << meta.altitude << ","
            << std::setprecision(2) << meta.roll << ","
            << meta.pitch << ","
            << meta.yaw << "\n";

    csvFile.close();
}

bool ExtractSEIMetadata(std::vector<uint8_t>& nalu, uint8_t naluType, SEIMetadata& meta, const std::string& csvFilePath)
{
    auto result = _GetSEIRawData(nalu, naluType);
    if (!result.second)
        return false;

    auto& seiPayload = result.first;

    // New format (0xFE 0x01)
    if (seiPayload.size() >= sizeof(FmSdSei) && seiPayload[0] == 0xFE && seiPayload[1] == 0x01)
    {
        FmSdSei sei;
        memcpy(&sei, seiPayload.data(), sizeof(FmSdSei));

        meta.latitude  = sei.uav_lat * 1e-7;
        meta.longitude = sei.uav_lon * 1e-7;
        meta.altitude  = sei.uav_alt / 1000.0;
        meta.roll  = sei.cam_phi / 100.0f;
        meta.pitch = sei.cam_the / 100.0f;
        meta.yaw   = sei.cam_psi / 100.0f;
        meta.valid = true;

        AppendSEIMetadataToCSV(meta, csvFilePath);  // 写 CSV

        return true;
    }

    // Old format (0xEE 0x16)
    else if (seiPayload.size() >= sizeof(SdSei) && seiPayload[0] == 0xEE && seiPayload[1] == 0x16)
    {
        SdSei sei;
        memcpy(&sei, seiPayload.data(), sizeof(SdSei));

        meta.latitude  = sei.uav_lat * 1e-7;
        meta.longitude = sei.uav_lon * 1e-7;
        meta.altitude  = sei.uav_alt / 1000.0;
        meta.roll  = sei.cam_phi / 100.0f;
        meta.pitch = sei.cam_the / 100.0f;
        meta.yaw   = sei.cam_psi / 100.0f;
        meta.valid = true;

        AppendSEIMetadataToCSV(meta, csvFilePath);  // 写 CSV
        
        return true;
    }

    return false;
}
