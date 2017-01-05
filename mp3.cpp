#include "mp3.h"

#include "External/inc/mpeg.h"
#include "External/inc/tag.h"
 
#include <unordered_map>
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


class CMP3 final : public IMP3
{
public:
	template<typename... Args >
	static std::shared_ptr<CMP3> create(Args&&... args)
	{
		return std::make_shared<CMP3>(std::forward<Args>(args)...);
	}

	CMP3(const std::string& f_path);
	CMP3(const uchar* f_data, const size_t f_size): CMP3() { parse(f_data, f_size); }

	std::shared_ptr<MPEG::IStream>	mpegStream		() const final override { return m_mpeg;	}

	std::shared_ptr<Tag::IID3v1>	tagID3v1		() const final override { return m_id3v1;	}
	std::shared_ptr<Tag::IID3v2>	tagID3v2		() const final override { return m_id3v2;	}
	std::shared_ptr<Tag::IAPE>		tagAPE			() const final override { return m_ape;		}
	std::shared_ptr<Tag::ILyrics>	tagLyrics		() const final override { return m_lyrics;	}

	unsigned						mpegStreamOffset() const final override { return m_offsets.at(DataType::MPEG		); }
	unsigned						tagID3v1Offset	() const final override { return m_offsets.at(DataType::TagID3v1	); }
	unsigned						tagID3v2Offset	() const final override { return m_offsets.at(DataType::TagID3v2	); }
	unsigned						tagAPEOffset	() const final override { return m_offsets.at(DataType::TagAPE		); }
	unsigned						tagLyricsOffset	() const final override { return m_offsets.at(DataType::TagLyrics	); }

	bool							hasIssues		() const final override
	{
		return (m_mpeg && m_mpeg->hasIssues()) || (m_id3v2 && m_id3v2->hasIssues()) || m_warnings;
	}

	bool							serialize		(const std::string& /*f_path*/) final override
	{
		ASSERT(!"Not implemented");
	}

private:
	enum class DataType : unsigned
	{
		MPEG, TagID3v1, TagID3v2, TagAPE, TagLyrics
	};
	template<typename T>
	struct EnumHasher
	{
		unsigned operator()(const T& f_key) const { return static_cast<unsigned>(f_key); }
	};
	using offsets_t = std::unordered_map<DataType, unsigned, EnumHasher<DataType>>;

private:
	explicit CMP3(): m_warnings(0) {}

	void parse(const uchar* f_data, const size_t f_size);

	template<typename T>
	bool tryCreateIfEmpty(DataType f_type, const uchar* f_data, size_t& ioOffset, size_t& ioSize, size_t f_tagSize, std::shared_ptr<T>& f_outTag)
	{
		if(f_outTag)
			return false;

		auto tagSize = f_tagSize ? f_tagSize : T::getSize(f_data, static_cast<size_t>(ioOffset), static_cast<size_t>(ioSize));
		if(!tagSize)
			return false;

		f_outTag = T::create(f_data, static_cast<size_t>(ioOffset), tagSize);
		m_offsets[f_type] = ioOffset;

		ioOffset += tagSize;
		ioSize -= tagSize;

		return true;
	}

private:
	std::vector<uchar>				m_vPreStream;
	std::shared_ptr<MPEG::IStream>	m_mpeg;
	std::vector<uchar>				m_vPostStream;
	// Tags
	std::shared_ptr<Tag::IID3v1>	m_id3v1;
	std::shared_ptr<Tag::IID3v2>	m_id3v2;
	std::shared_ptr<Tag::IAPE>		m_ape;
	std::shared_ptr<Tag::ILyrics>	m_lyrics;

	offsets_t						m_offsets;

	uint							m_warnings;

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


template<typename T>
static uint findTag(const uchar* f_data, size_t f_size, size_t f_scanSize)
{
	for(size_t rfo = 0, sz = f_size, n = f_scanSize; sz && n; ++rfo, --sz, --n)
	{
		// + Relative Frame Offset
		if(auto tagSize = T::getSize(f_data, rfo, sz) )
		{
			ASSERT(f_data + rfo + tagSize >= f_data);
			return rfo;
		}
	}

	return f_scanSize;
}

void CMP3::parse(const uchar* f_data, const size_t f_size)
{
	for(size_t offset = 0, unprocessed = f_size; offset < f_size;)
	{
		ASSERT(unprocessed <= f_size);

		// MPEG stream
		if(!m_mpeg && MPEG::IStream::verifyFrameSequence(f_data + offset, unprocessed))
		{
			auto pData = f_data + offset;

			m_mpeg = MPEG::IStream::create(pData, unprocessed);
			m_offsets[DataType::MPEG] = offset;

			// Check the last frame for unexpected data
			ASSERT(m_mpeg->getFrameCount());
			auto uLast = m_mpeg->getFrameCount() - 1;
			auto uRelLastOffset = m_mpeg->getFrameOffset(uLast);

			uint o;
			do
			{
				auto uLastOffset = offset + uRelLastOffset;
				auto uLastSize = m_mpeg->getFrameSize(uLast);
				auto size = unprocessed - uRelLastOffset;

				// APE
				o = findTag<Tag::IAPE>(pData + uRelLastOffset, size, uLastSize);
				if(o < uLastSize)
				{
					WARNING("APE tag in the last MPEG frame @ " << (uLastOffset + o) << " (0x" << OUT_HEX(uLastOffset + o) << ") - keep the tag, discard the frame");
					auto removed = m_mpeg->truncate(1);
					ASSERT(removed == 1);
					ASSERT(uRelLastOffset == m_mpeg->getSize());
					break;
				}

				// Lyrics
				o = findTag<Tag::ILyrics>(pData + uRelLastOffset, size, uLastSize);
				if(o < uLastSize)
				{
					WARNING("Lyrics tag in the last MPEG frame @ " << (uLastOffset + o) << " (0x" << OUT_HEX(uLastOffset + o) << ") - keep the tag, discard the frame");
					auto removed = m_mpeg->truncate(1);
					ASSERT(removed == 1);
					ASSERT(uRelLastOffset == m_mpeg->getSize());
					break;
				}
			}
			while(0);

			o += uRelLastOffset;
			offset += o;
			unprocessed -= o;
			continue;
		}

		// Tags
		if( tryCreateIfEmpty(DataType::TagID3v1, f_data, offset, unprocessed, 0, m_id3v1) )
		{
			auto o = offset - m_id3v1->getSize();
			if(unprocessed)
				WARNING("ID3v1 tag @ invalid offset " << o << " (0x" << OUT_HEX(o) << ')');
			continue;
		}
		if( tryCreateIfEmpty(DataType::TagID3v2, f_data, offset, unprocessed, 0, m_id3v2) )
			continue;
		if( tryCreateIfEmpty(DataType::TagAPE, f_data, offset, unprocessed, 0, m_ape) )
			continue;
		if( tryCreateIfEmpty(DataType::TagLyrics, f_data, offset, unprocessed, 0, m_lyrics) )
			continue;

		// Check for padding nulls
		if(f_data[offset] == 0)
		{
			++offset;
			--unprocessed;
			continue;
		}

		if(m_mpeg)
		{
			// Check for an incomplete frame
			if( MPEG::IStream::isIncompleteFrame(f_data + offset, unprocessed) )
			{
				// The corresponding warning is emited by the MPEG library
				offset += unprocessed;
				unprocessed -= unprocessed;
				continue;
			}

			// Post MPEG stream garbage?
			ASSERT(m_vPostStream.empty());

			auto oPrev = offset;
			auto uPrev = unprocessed;
			for(; unprocessed; ++offset, --unprocessed)
			{
				if( Tag::IAPE::getSize(f_data, offset, unprocessed) )
					break;
				if( Tag::IID3v1::getSize(f_data, offset, unprocessed) )
					break;
				if( Tag::ILyrics::getSize(f_data, offset, unprocessed) )
					break;

				ASSERT(Tag::IID3v2::getSize(f_data, offset, unprocessed) == 0);
			}

			// Might be zero if APE footer-only tag is found
			if(auto sz = uPrev - unprocessed)
			{
				WARNING(sz << " (0x" << OUT_HEX(sz) << ") bytes of garbage after the MPEG stream @ " <<
						oPrev << " (0x" << OUT_HEX(oPrev) << ')');

				m_vPostStream.resize(sz);
				memcpy(&m_vPostStream[0], f_data + oPrev, sz);
			}
			continue;
		}
		else
		{
			// Pre MPEG stream garbage?
			ASSERT(m_vPreStream.empty());

			auto oPrev = offset;
			auto uPrev = unprocessed;
			for(; unprocessed; ++offset, --unprocessed)
			{
				if(MPEG::IStream::verifyFrameSequence(f_data + offset, unprocessed))
					break;

				ASSERT(Tag::IID3v1	::getSize(f_data, offset, unprocessed) == 0);
				ASSERT(Tag::IID3v2	::getSize(f_data, offset, unprocessed) == 0);
				ASSERT(Tag::IAPE	::getSize(f_data, offset, unprocessed) == 0);
				ASSERT(Tag::ILyrics	::getSize(f_data, offset, unprocessed) == 0);
			}

			if(unprocessed)
			{
				auto sz = uPrev - unprocessed;
				WARNING(sz << " (0x" << OUT_HEX(sz) << ") bytes of garbage before the MPEG stream @ " <<
						oPrev << " (0x" << OUT_HEX(oPrev) << ')');

				m_vPreStream.resize(sz);
				memcpy(&m_vPreStream[0], f_data + offset, sz);
				continue;
			}
		}

		throw exc_bad_data(offset);
	}

	if(!m_mpeg)
		WARNING("no MPEG stream");
}

// ============================================================================
std::shared_ptr<IMP3> IMP3::create(const unsigned char* f_data, size_t f_size)
{
	return CMP3::create(f_data, f_size);
}

std::shared_ptr<IMP3> IMP3::create(const std::string& f_path)
{
	return CMP3::create(f_path);
}

IMP3::~IMP3() {}

