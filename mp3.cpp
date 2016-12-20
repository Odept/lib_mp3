#include "mp3.h"

#include "External/inc/mpeg.h"
#include "External/inc/tag.h"
 
#include <fstream>
#include <sstream>

#define __STR_INTERNAL(x) #x
#define __STR(x) __STR_INTERNAL(x)
#define STR__LINE__ __STR(__LINE__)
#define ASSERT(X)	if(!(X)) throw std::logic_error(#X " @ " __FILE__ ":" STR__LINE__)

#include <iostream>
#define OUT_HEX(X)	std::hex << (X) << std::dec
#define WARNING(X)	do { std::cerr << "WARNING @ " << __FILE__ << ":" << __LINE__ << ": " << X << std::endl; ++m_warnings; } while(0)


using uint		= unsigned int;
using ushort	= unsigned short;
using uchar		= unsigned char;


/******************************************************************************
 * CMP3
 *****************************************************************************/
class CMP3 : public IMP3
{
public:
	template<typename... Args >
	static std::shared_ptr<CMP3> create(Args&&... args)
	{
		return std::make_shared<CMP3>(std::forward<Args>(args)...);
	}

	CMP3(const std::string& f_path);
	CMP3(const uchar* f_data, const size_t f_size): CMP3() { parse(f_data, f_size); }

	std::shared_ptr<MPEG::IStream>	mpegStream	() const final override { return m_mpeg;	}

	std::shared_ptr<Tag::IID3v1>	tagID3v1	() const final override { return m_id3v1;	}
	std::shared_ptr<Tag::IID3v2>	tagID3v2	() const final override { return m_id3v2;	}
	std::shared_ptr<Tag::IAPE>		tagAPE		() const final override { return m_ape;		}
	std::shared_ptr<Tag::ILyrics>	tagLyrics	() const final override { return m_lyrics;	}

	bool							hasWarnings		() const final override
	{
		return (m_mpeg && m_mpeg->hasWarnings()) || m_warnings;
	}

	bool							serialize	(const std::string& /*f_path*/) final override { ASSERT(!"Not implemented"); }

private:
	explicit CMP3(): m_warnings(0) {}

	void parse(const uchar* f_data, const size_t f_size);

	template<typename T>
	static bool tryCreateIfEmpty(const uchar* f_data, size_t& ioSize, size_t& ioOffset, std::shared_ptr<T>& f_outTag)
	{
		if(f_outTag != nullptr)
			return false;

		auto tagSize = T::getSize(f_data, ioSize);
		if(!tagSize)
			return false;

		f_outTag = T::create(f_data, tagSize);
		//m_dataMap.push_back( DataMapEntry(DMET_TAG_ID3v1, offset) );

		ioOffset += tagSize;
		ioSize -= tagSize;

		return true;
	}

private:
	std::shared_ptr<MPEG::IStream>	m_mpeg;

	// Tags
	std::shared_ptr<Tag::IID3v1>	m_id3v1;
	std::shared_ptr<Tag::IID3v2>	m_id3v2;
	std::shared_ptr<Tag::IAPE>		m_ape;
	std::shared_ptr<Tag::ILyrics>	m_lyrics;

	uint							m_warnings;

private:
	/*enum DataMapEntryType
	{
		DET_TAG_v1,
		DET_TAG_v2,
		DET_TAG_APE
		DET_TAG_LYRICS,
		DET_MPEG,
		DET_PADDING
	};


	struct DataMepEntry
	{
		sizr_t				Offset;
		DataMapEntryType	Type;

		DataMepEntry(size_t offset, DataMapEntryType type): Offset(offset), Type(type) {}
	};
	std::vector<DataMepEntry> m_dataMap;*/

	// Exceptions
private:
	class exc_mp3 : public IMP3::exception
	{
	public:
		const char* what() const noexcept final override { return m_text.c_str(); }
	protected:
		std::string m_text;
	};


	class exc_bad_data : public exc_mp3
	{
	public:
		exc_bad_data(size_t f_offset)
		{
			std::ostringstream oss;
			oss << "Unsupported data @ " << f_offset << " (0x" << OUT_HEX(f_offset) << ')';
			m_text = oss.str();
		}
	};


	class exc_bad_file : public exc_mp3
	{
	public:
		exc_bad_file(const std::string& f_path)
		{
			std::ostringstream oss;
			oss << "Failed to open \"" << f_path << '"';
			m_text = oss.str();
		}
	};


	class exc_bad_file_read : public exc_mp3 //exc_bad_file
	{
	public:
		exc_bad_file_read(const std::string& f_path, size_t f_actual, size_t f_expected)
		{
			std::ostringstream oss;
			oss << "Failed to read file \"" << f_path << "\" (" << f_actual << " of " << f_expected << " bytes read)";
			m_text = oss.str();
		}
	};
};

// ============================================================================
CMP3::CMP3(const std::string& f_path):
	CMP3()
{
	std::ifstream file(f_path.c_str(), std::ifstream::in | std::ifstream::binary);
	if(!file.is_open())
		throw exc_bad_file(f_path);

	std::filebuf* pFileBuf = file.rdbuf();

	std::streampos size = pFileBuf->pubseekoff(0, file.end, file.in);
	pFileBuf->pubseekpos(0, file.in);

	std::vector<uchar> data(size);
	auto read = pFileBuf->sgetn(reinterpret_cast<char*>(&data[0]), size);
	if(read != size)
		throw exc_bad_file_read(f_path, read, size);

	parse(&data[0], size);
}


void CMP3::parse(const uchar* f_data, const size_t f_size)
{
	for(size_t offset = 0, unprocessed = f_size; offset < f_size;)
	{
		auto pData = f_data + offset;
		ASSERT(unprocessed <= f_size);

		// MPEG stream
		if(!m_mpeg && MPEG::IStream::verifyFrameSequence(pData, unprocessed))
		{
			m_mpeg = MPEG::IStream::create(pData, unprocessed);
			//m_dataMap.push_back( DataMapEntry(DMET_MPEG, offset) );

			// Check the last frame for the invalid data
			ASSERT(m_mpeg->getFrameCount());
			uint uLast = m_mpeg->getFrameCount() - 1;
			uint uLastOffset = offset + m_mpeg->getFrameOffset(uLast);

			for(uint o = 0, n = m_mpeg->getFrameSize(uLast); n; o++, n--)
			{
				if(Tag::IAPE::getSize(f_data + uLastOffset + o, n) == 0)
					continue;

				WARNING("APE tag in the last MPEG frame @ " << (uLastOffset + o) << " (0x" << OUT_HEX(uLastOffset + o) << ") - keep the tag, discard the frame");

				auto removed = m_mpeg->truncate(1);
				ASSERT(removed == 1);
				offset += o;
				unprocessed -= o;
				break;
			}

			offset += m_mpeg->getSize();
			unprocessed -= m_mpeg->getSize();
			continue;
		}

		// Tags
		if( tryCreateIfEmpty(pData, unprocessed, offset, m_id3v1) )
		{
			if(unprocessed)
				WARNING("ID3v1 tag @ invalid offset " << offset << " (0x" << OUT_HEX(offset) << ')');
			continue;
		}
		if( tryCreateIfEmpty(pData, unprocessed, offset, m_id3v2) )
			continue;
		if( tryCreateIfEmpty(pData, unprocessed, offset, m_ape) )
			continue;
		if( tryCreateIfEmpty(pData, unprocessed, offset, m_lyrics) )
			continue;

		// Check for padding nulls
		if(f_data[offset] == 0)
		{
			++offset;
			--unprocessed;
			continue;
		}
			
		// Check for an incomplete frame
		if( MPEG::IStream::isIncompleteFrame(pData, unprocessed) )
		{
			WARNING("Unexpected end of frame @ " << offset << " (0x" << OUT_HEX(offset) << ") - discard");

			offset += unprocessed;
			unprocessed -= unprocessed;
			continue;
		}

		throw exc_bad_data(offset);
	}
}

/******************************************************************************
 * IMP3
 *****************************************************************************/
std::shared_ptr<IMP3> IMP3::create(const unsigned char* f_data, size_t f_size)
{
	return CMP3::create(f_data, f_size);
}

std::shared_ptr<IMP3> IMP3::create(const std::string& f_path)
{
	return CMP3::create(f_path);
}

IMP3::~IMP3() {}

