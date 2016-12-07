// ASSERTs to throw everywhere, including libraries
#include "mp3.h"

#include "External/inc/mpeg.h"
#include "External/inc/tag.h"
 
#include <fstream>

#include <iostream>
#define OUT_HEX(X)	std::hex << (X) << std::dec
#define ASSERT(X)	if(!(X)) { std::cout << "Abort @ " << __FILE__ << ":" << __LINE__ << ": \"" << #X << "\"" << std::endl; std::abort(); }
#define ERROR(X)	do { std::cerr << "ERROR: " << X << std::endl; } while(0)
#define WARNING(X)	do { std::cerr << "WARNING: " << X << std::endl; } while(0)


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
	static std::shared_ptr<CMP3> create(Args&&... args);

	CMP3(const std::string& f_path);
	CMP3(const uchar* f_data, const size_t f_size): CMP3() { parse(f_data, f_size); }

	std::shared_ptr<MPEG::IStream>	mpegStream	() const final override { return m_mpeg;	}

	std::shared_ptr<Tag::IID3v1>	tagID3v1	() const final override { return m_id3v1;	}
	std::shared_ptr<Tag::IID3v2>	tagID3v2	() const final override { return m_id3v2;	}
	std::shared_ptr<Tag::IAPE>		tagAPE		() const final override { return m_ape;		}
	std::shared_ptr<Tag::ILyrics>	tagLyrics	() const final override { return m_lyrics;	}

	bool							isCononical	() const final override { return m_mpeg && !m_warnings; }

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
public:
	class exc_bad_data : public std::exception
	{
	public:
		exc_bad_data(size_t offset): m_offset(offset) {}
		size_t offset() const { return m_offset; }

	private:
		const size_t m_offset;
	};


	class exc_bad_file : public std::exception
	{
	public:
		exc_bad_file(const std::string& f_path): m_path(f_path) {}
		const std::string& file() const { return m_path; }

	private:
		std::string m_path;
	};


	class exc_bad_file_read : public exc_bad_file
	{
	public:
		exc_bad_file_read(const std::string& f_path, size_t f_actual, size_t f_expected):
			exc_bad_file(f_path),
			m_actual(f_actual),
			m_expected(f_expected)
		{}
		size_t actual() const { return m_actual; }
		size_t expected() const { return m_expected; }

	private:
		size_t m_actual;
		size_t m_expected;
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
				++m_warnings;

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
			{
				WARNING("ID3v1 tag @ invalid offset " << offset << " (0x" << OUT_HEX(offset) << ')');
				++m_warnings;
			}
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
			++m_warnings;

			offset += unprocessed;
			unprocessed -= unprocessed;
			continue;
		}

		throw exc_bad_data(offset);
	}
}


template<typename... Args >
std::shared_ptr<CMP3> CMP3::create(Args&&... args)
{
	std::shared_ptr<CMP3> sp;

	try
	{
		sp = std::make_shared<CMP3>(std::forward<Args>(args)...);
	}
	catch(const exc_bad_file_read& e)
	{
		ERROR("Failed to read file \"" << e.file() << "\" (" << e.actual() << " of " << e.expected() << " bytes read)");
	}
	catch(const exc_bad_file& e)
	{
		ERROR("Failed to open \"" << e.file() << '"');
	}
	catch(const exc_bad_data& e)
	{
		ERROR("Unsupported data @ " << e.offset() << " (0x" << OUT_HEX(e.offset()) << ')');
	}
	catch(...)
	{
		ASSERT(!"unexpected");
	}

	return sp;
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

