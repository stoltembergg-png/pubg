#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "ioctl_protocol.h"

namespace
{
    constexpr uint32_t kKnownFlags = 0u;

    bool CheckedAdd32(uint32_t left, uint32_t right, uint32_t* result)
    {
        if (right > std::numeric_limits<uint32_t>::max() - left)
            return false;
        *result = left + right;
        return true;
    }

    bool CheckedAdd64(uint64_t left, uint64_t right, uint64_t* result)
    {
        if (right > std::numeric_limits<uint64_t>::max() - left)
            return false;
        *result = left + right;
        return true;
    }

    bool IsValidLength(uint32_t length)
    {
        return length != 0u && length <= PUBGEXT_MAX_TRANSFER;
    }

    bool IsValidAddressRange(uint64_t address, uint32_t length)
    {
        uint64_t end = 0;
        return IsValidLength(length) && CheckedAdd64(address, length, &end);
    }

    bool IsExactSize(uint32_t actual, uint32_t expected)
    {
        return actual == expected;
    }

    bool IsValidRequestHeader(const PUBGEXT_REQUEST_HEADER& header,
                              uint32_t expectedSize)
    {
        return header.magic == PUBGEXT_IOCTL_MAGIC &&
               header.major == PUBGEXT_PROTOCOL_MAJOR &&
               header.struct_size == expectedSize &&
               header.flags == kKnownFlags &&
               header.reserved == 0u;
    }

    bool AreReadReservedFieldsZero(const PUBGEXT_READ_REQUEST& request)
    {
        return request.header.reserved == 0u && request.reserved0 == 0u &&
               request.reserved1 == 0u;
    }

    bool AreWriteReservedFieldsZero(const PUBGEXT_WRITE_REQUEST& request)
    {
        return request.header.reserved == 0u && request.reserved0 == 0u &&
               request.reserved1 == 0u;
    }

    bool CalculateFramedLength(uint64_t fixedSize, uint32_t length, uint64_t* total)
    {
        return IsValidLength(length) && CheckedAdd64(fixedSize, length, total);
    }

    bool IsReadFrame(uint64_t outputBufferLength, uint32_t length)
    {
        uint64_t expected = 0;
        return CalculateFramedLength(PUBGEXT_RESPONSE_FIXED_SIZE, length, &expected) &&
               outputBufferLength == expected;
    }

    bool IsWriteFrame(uint64_t inputBufferLength, uint32_t length)
    {
        uint64_t expected = 0;
        return CalculateFramedLength(PUBGEXT_WRITE_FIXED_SIZE, length, &expected) &&
               inputBufferLength == expected;
    }
}

TEST(IoctlProtocolTest, IoctlValuesUseExpectedCtlCodeFormula)
{
    EXPECT_EQ(PUBGEXT_IOCTL_AUTH,
              PUBGEXT_IOCTL_CTL_CODE(PUBGEXT_IOCTL_DEVICE_TYPE, 0x800u,
                                     PUBGEXT_IOCTL_METHOD_BUFFERED,
                                     PUBGEXT_IOCTL_FILE_READ_ACCESS |
                                         PUBGEXT_IOCTL_FILE_WRITE_ACCESS));
    EXPECT_EQ(PUBGEXT_IOCTL_QUERY_CAPS,
              PUBGEXT_IOCTL_CTL_CODE(PUBGEXT_IOCTL_DEVICE_TYPE, 0x801u,
                                     PUBGEXT_IOCTL_METHOD_BUFFERED,
                                     PUBGEXT_IOCTL_FILE_READ_ACCESS |
                                         PUBGEXT_IOCTL_FILE_WRITE_ACCESS));
    EXPECT_EQ(PUBGEXT_IOCTL_READ,
              PUBGEXT_IOCTL_CTL_CODE(PUBGEXT_IOCTL_DEVICE_TYPE, 0x802u,
                                     PUBGEXT_IOCTL_METHOD_BUFFERED,
                                     PUBGEXT_IOCTL_FILE_READ_ACCESS |
                                         PUBGEXT_IOCTL_FILE_WRITE_ACCESS));
    EXPECT_EQ(PUBGEXT_IOCTL_WRITE,
              PUBGEXT_IOCTL_CTL_CODE(PUBGEXT_IOCTL_DEVICE_TYPE, 0x803u,
                                     PUBGEXT_IOCTL_METHOD_BUFFERED,
                                     PUBGEXT_IOCTL_FILE_READ_ACCESS |
                                         PUBGEXT_IOCTL_FILE_WRITE_ACCESS));

    EXPECT_EQ(PUBGEXT_IOCTL_AUTH, 0x8337E000u);
    EXPECT_EQ(PUBGEXT_IOCTL_QUERY_CAPS, 0x8337E004u);
    EXPECT_EQ(PUBGEXT_IOCTL_READ, 0x8337E008u);
    EXPECT_EQ(PUBGEXT_IOCTL_WRITE, 0x8337E00Cu);
}

TEST(IoctlProtocolTest, ProtocolSizesAndCriticalOffsetsAreStable)
{
    EXPECT_EQ(sizeof(PUBGEXT_REQUEST_HEADER), 32u);
    EXPECT_EQ(sizeof(PUBGEXT_RESPONSE_HEADER), 40u);
    EXPECT_EQ(sizeof(PUBGEXT_READ_REQUEST), 56u);
    EXPECT_EQ(PUBGEXT_WRITE_FIXED_SIZE, 56u);
    EXPECT_EQ(sizeof(PUBGEXT_QUERY_CAPS_RESPONSE), 48u);

    EXPECT_EQ(PUBGEXT_OFFSET_OF(PUBGEXT_REQUEST_HEADER, magic), 0u);
    EXPECT_EQ(PUBGEXT_OFFSET_OF(PUBGEXT_REQUEST_HEADER, major), 4u);
    EXPECT_EQ(PUBGEXT_OFFSET_OF(PUBGEXT_REQUEST_HEADER, minor), 6u);
    EXPECT_EQ(PUBGEXT_OFFSET_OF(PUBGEXT_REQUEST_HEADER, request_id), 16u);
    EXPECT_EQ(PUBGEXT_OFFSET_OF(PUBGEXT_RESPONSE_HEADER, operation_status), 24u);
    EXPECT_EQ(PUBGEXT_OFFSET_OF(PUBGEXT_RESPONSE_HEADER, transferred), 32u);
    EXPECT_EQ(PUBGEXT_OFFSET_OF(PUBGEXT_READ_REQUEST, pid), 32u);
    EXPECT_EQ(PUBGEXT_OFFSET_OF(PUBGEXT_READ_REQUEST, remote_va), 40u);
    EXPECT_EQ(PUBGEXT_OFFSET_OF(PUBGEXT_READ_REQUEST, length), 48u);
    EXPECT_EQ(PUBGEXT_OFFSET_OF(PUBGEXT_WRITE_REQUEST, data), 56u);
}

TEST(IoctlProtocolTest, ProtocolConstantsAndCapabilitiesMatchDrawing)
{
    EXPECT_EQ(PUBGEXT_IOCTL_DEVICE_TYPE, 0x8337u);
    EXPECT_EQ(PUBGEXT_IOCTL_MAGIC, 0x50554247u);
    EXPECT_EQ(PUBGEXT_PROTOCOL_MAJOR, 2u);
    EXPECT_EQ(PUBGEXT_PROTOCOL_MINOR, 0u);
    EXPECT_EQ(PUBGEXT_MAX_TRANSFER, 16u * 1024u * 1024u);
    EXPECT_EQ(PUBGEXT_COPY_CHUNK, 64u * 1024u);

    EXPECT_EQ(PUBGEXT_CAP_AUTH, 0x00000001u);
    EXPECT_EQ(PUBGEXT_CAP_QUERY, 0x00000002u);
    EXPECT_EQ(PUBGEXT_CAP_READ, 0x00000004u);
    EXPECT_EQ(PUBGEXT_CAP_WRITE, 0x00000008u);
    EXPECT_EQ(PUBGEXT_CAP_VIRTUAL, 0x00000010u);
    EXPECT_EQ(PUBGEXT_CAP_AUTH | PUBGEXT_CAP_QUERY | PUBGEXT_CAP_READ |
                  PUBGEXT_CAP_WRITE | PUBGEXT_CAP_VIRTUAL,
              0x0000001Fu);
}

TEST(IoctlProtocolTest, RejectsInvalidLengthsAndAddressOverflows)
{
    EXPECT_FALSE(IsValidLength(0u));
    EXPECT_TRUE(IsValidLength(PUBGEXT_MAX_TRANSFER));
    EXPECT_FALSE(IsValidLength(PUBGEXT_MAX_TRANSFER + 1u));

    uint32_t end32 = 0;
    EXPECT_TRUE(CheckedAdd32(std::numeric_limits<uint32_t>::max() - 1u, 1u, &end32));
    EXPECT_EQ(end32, std::numeric_limits<uint32_t>::max());
    EXPECT_FALSE(CheckedAdd32(std::numeric_limits<uint32_t>::max(), 1u, &end32));

    uint64_t end64 = 0;
    EXPECT_TRUE(CheckedAdd64(std::numeric_limits<uint64_t>::max() - 1u, 1u, &end64));
    EXPECT_EQ(end64, std::numeric_limits<uint64_t>::max());
    EXPECT_FALSE(CheckedAdd64(std::numeric_limits<uint64_t>::max(), 1u, &end64));

    EXPECT_TRUE(IsValidAddressRange(0x1000u, 1u));
    EXPECT_FALSE(IsValidAddressRange(std::numeric_limits<uint64_t>::max(), 1u));
    EXPECT_FALSE(IsValidAddressRange(std::numeric_limits<uint64_t>::max() - 1u, 2u));
}

TEST(IoctlProtocolTest, RequiresExactStructSizes)
{
    const uint32_t sizes[] = {
        static_cast<uint32_t>(sizeof(PUBGEXT_REQUEST_HEADER)),
        static_cast<uint32_t>(sizeof(PUBGEXT_RESPONSE_HEADER)),
        static_cast<uint32_t>(sizeof(PUBGEXT_READ_REQUEST)),
        static_cast<uint32_t>(sizeof(PUBGEXT_QUERY_CAPS_RESPONSE)),
    };

    for (const uint32_t size : sizes)
    {
        EXPECT_TRUE(IsExactSize(size, size));
        EXPECT_FALSE(IsExactSize(size - 1u, size));
        EXPECT_FALSE(IsExactSize(size + 1u, size));
    }
}

TEST(IoctlProtocolTest, RejectsBadMagicVersionSizeReservedAndFlags)
{
    PUBGEXT_REQUEST_HEADER header{};
    header.magic = PUBGEXT_IOCTL_MAGIC;
    header.major = PUBGEXT_PROTOCOL_MAJOR;
    header.minor = PUBGEXT_PROTOCOL_MINOR;
    header.struct_size = PUBGEXT_REQUEST_FIXED_SIZE;

    EXPECT_TRUE(IsValidRequestHeader(header, PUBGEXT_REQUEST_FIXED_SIZE));

    header.magic = 0u;
    EXPECT_FALSE(IsValidRequestHeader(header, PUBGEXT_REQUEST_FIXED_SIZE));
    header.magic = PUBGEXT_IOCTL_MAGIC;

    header.major = PUBGEXT_PROTOCOL_MAJOR - 1u;
    EXPECT_FALSE(IsValidRequestHeader(header, PUBGEXT_REQUEST_FIXED_SIZE));
    header.major = PUBGEXT_PROTOCOL_MAJOR;

    header.struct_size = PUBGEXT_REQUEST_FIXED_SIZE - 1u;
    EXPECT_FALSE(IsValidRequestHeader(header, PUBGEXT_REQUEST_FIXED_SIZE));
    header.struct_size = PUBGEXT_REQUEST_FIXED_SIZE;

    header.reserved = 1u;
    EXPECT_FALSE(IsValidRequestHeader(header, PUBGEXT_REQUEST_FIXED_SIZE));
    header.reserved = 0u;

    header.flags = 1u;
    EXPECT_FALSE(IsValidRequestHeader(header, PUBGEXT_REQUEST_FIXED_SIZE));

    PUBGEXT_READ_REQUEST read{};
    EXPECT_TRUE(AreReadReservedFieldsZero(read));
    read.reserved0 = 1u;
    EXPECT_FALSE(AreReadReservedFieldsZero(read));
    read.reserved0 = 0u;
    read.reserved1 = 1u;
    EXPECT_FALSE(AreReadReservedFieldsZero(read));

    PUBGEXT_WRITE_REQUEST write{};
    EXPECT_TRUE(AreWriteReservedFieldsZero(write));
    write.reserved0 = 1u;
    EXPECT_FALSE(AreWriteReservedFieldsZero(write));
    write.reserved0 = 0u;
    write.reserved1 = 1u;
    EXPECT_FALSE(AreWriteReservedFieldsZero(write));
}

TEST(IoctlProtocolTest, FramingRequiresFixedHeaderPlusPayload)
{
    EXPECT_EQ(PUBGEXT_WRITE_FIXED_SIZE, 56u);

    uint32_t frame32 = 0;
    EXPECT_TRUE(CheckedAdd32(PUBGEXT_WRITE_FIXED_SIZE, PUBGEXT_MAX_TRANSFER, &frame32));
    EXPECT_EQ(frame32, PUBGEXT_WRITE_FIXED_SIZE + PUBGEXT_MAX_TRANSFER);
    EXPECT_FALSE(CheckedAdd32(std::numeric_limits<uint32_t>::max() - 1u, 2u, &frame32));

    EXPECT_TRUE(IsReadFrame(40u + 1024u, 1024u));
    EXPECT_FALSE(IsReadFrame(40u + 1023u, 1024u));
    EXPECT_FALSE(IsReadFrame(40u + 1024u, 0u));

    EXPECT_TRUE(IsWriteFrame(56u + 1024u, 1024u));
    EXPECT_FALSE(IsWriteFrame(56u + 1023u, 1024u));
    EXPECT_FALSE(IsWriteFrame(56u + 1024u, 0u));
    EXPECT_FALSE(IsWriteFrame(56u + PUBGEXT_MAX_TRANSFER + 1u,
                              PUBGEXT_MAX_TRANSFER + 1u));

    uint64_t total = 0;
    EXPECT_FALSE(CalculateFramedLength(std::numeric_limits<uint64_t>::max(), 1u, &total));
}
