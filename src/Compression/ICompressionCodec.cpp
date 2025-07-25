#include <Compression/ICompressionCodec.h>

#include <cassert>

#include <Parsers/ASTFunction.h>
#include <base/unaligned.h>
#include <Common/Exception.h>
#include <Common/CurrentMetrics.h>
#include <Common/SipHash.h>
#include <Parsers/ASTIdentifier.h>
#include <Compression/CompressionCodecMultiple.h>


namespace CurrentMetrics
{
    extern const Metric Compressing;
    extern const Metric Decompressing;
}

namespace DB
{

namespace ErrorCodes
{
    extern const int LOGICAL_ERROR;
}


void ICompressionCodec::setCodecDescription(const String & codec_name, const ASTs & arguments)
{
    std::shared_ptr<ASTFunction> result = std::make_shared<ASTFunction>();
    result->name = "CODEC";

    /// Special case for codec Multiple, which doesn't have name. It's just list
    /// of other codecs.
    if (codec_name.empty())
    {
        ASTPtr codec_desc = std::make_shared<ASTExpressionList>();
        for (const auto & argument : arguments)
            codec_desc->children.push_back(argument);
        result->arguments = codec_desc;
    }
    else
    {
        ASTPtr codec_desc;
        if (arguments.empty()) /// Codec without arguments is just ASTIdentifier
            codec_desc = std::make_shared<ASTIdentifier>(codec_name);
        else /// Codec with arguments represented as ASTFunction
            codec_desc = makeASTFunction(codec_name, arguments);

        result->arguments = std::make_shared<ASTExpressionList>();
        result->arguments->children.push_back(codec_desc);
    }

    result->children.push_back(result->arguments);
    full_codec_desc = result;
}


ASTPtr ICompressionCodec::getFullCodecDesc() const
{
    if (full_codec_desc == nullptr)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "Codec description is not prepared");

    return full_codec_desc;
}


ASTPtr ICompressionCodec::getCodecDesc() const
{
    auto arguments = getFullCodecDesc()->as<ASTFunction>()->arguments;
    /// If it has exactly one argument, than it's single codec, return it
    if (arguments->children.size() == 1)
        return arguments->children[0];
    /// Otherwise we have multiple codecs and return them as expression list
    return arguments;
}

UInt64 ICompressionCodec::getHash() const
{
    SipHash hash;
    updateHash(hash);
    return hash.get64();
}

UInt32 ICompressionCodec::compress(const char * source, UInt32 source_size, char * dest) const
{
    assert(source != nullptr && dest != nullptr);

    CurrentMetrics::Increment metric_increment(CurrentMetrics::Compressing);

    dest[0] = getMethodByte();
    UInt8 header_size = getHeaderSize();
    /// Write data from header_size
    UInt32 compressed_bytes_written = doCompressData(source, source_size, &dest[header_size]);
    unalignedStoreLittleEndian<UInt32>(&dest[1], compressed_bytes_written + header_size);
    unalignedStoreLittleEndian<UInt32>(&dest[5], source_size);
    return header_size + compressed_bytes_written;
}

UInt32 ICompressionCodec::decompress(const char * source, UInt32 source_size, char * dest) const
{
    assert(source != nullptr && dest != nullptr);

    CurrentMetrics::Increment metric_increment(CurrentMetrics::Decompressing);

    UInt8 header_size = getHeaderSize();
    if (source_size < header_size)
        throw Exception(decompression_error_code,
                        "Can't decompress data: the compressed data size ({}, this should include header size) "
                        "is less than the header size ({})", source_size, static_cast<size_t>(header_size));

    uint8_t our_method = getMethodByte();
    uint8_t method = source[0];
    if (method != our_method)
        throw Exception(decompression_error_code, "Can't decompress data with codec byte {} using codec with byte {}", method, our_method);

    UInt32 decompressed_size = readDecompressedBlockSize(source);
    doDecompressData(&source[header_size], source_size - header_size, dest, decompressed_size);

    return decompressed_size;
}

UInt32 ICompressionCodec::readCompressedBlockSize(const char * source) const
{
    UInt32 compressed_block_size = unalignedLoadLittleEndian<UInt32>(&source[1]);
    if (compressed_block_size == 0)
        throw Exception(decompression_error_code, "Can't decompress data: header is corrupt with compressed block size 0");
    return compressed_block_size;
}


UInt32 ICompressionCodec::readDecompressedBlockSize(const char * source) const
{
    UInt32 decompressed_block_size = unalignedLoadLittleEndian<UInt32>(&source[5]);
    if (decompressed_block_size == 0)
        throw Exception(decompression_error_code, "Can't decompress data: header is corrupt with decompressed block size 0");
    return decompressed_block_size;
}


uint8_t ICompressionCodec::readMethod(const char * source)
{
    return static_cast<uint8_t>(source[0]);
}

}
