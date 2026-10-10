#include "stdafx.h"

#include "Texts.h"
#include "BinChunkSerializer.h"
#include "FileSystem/FileStream.h"
#if defined(__linux__)
#include <iconv.h>
#endif
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
namespace
{
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static bool isCacheReadonly = false;
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef hash_map<string,wstring> TTexts;
inline TTexts& GetTextsCache()
{
  static TTexts cache;
  return cache;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
inline const wstring& GetEmptyString()
{
  static wstring empty;
  return empty;
}
#if defined(__linux__)
/// Decode the UTF-16LE payload after its BOM into native wchar_t, independent of locale.
bool ReadUtf16Payload(Stream* stream, int size, wstring* text)
{
	if (size % 2 != 0)
		return false;
	if (size == 0)
		return true;

	vector<char> bytes(size);
	if (stream->Read(&bytes[0], size) != size)
		return false;

	// One native character per UTF-16 code unit also leaves room for surrogate pairs.
	text->resize(size / 2, 0);
	iconv_t converter = iconv_open("WCHAR_T", "UTF-16LE");
	if (converter == (iconv_t)-1)
	{
		text->clear();
		return false;
	}
	char* input = &bytes[0];
	char* output = reinterpret_cast<char*>(&(*text)[0]);
	size_t inputBytes = size;
	const size_t capacity = text->size() * sizeof(wchar_t);
	size_t outputBytes = capacity;
	const bool valid = iconv(converter, &input, &inputBytes, &output, &outputBytes) != (size_t)-1
		&& inputBytes == 0;
	iconv_close(converter);
	text->resize(valid ? (capacity - outputBytes) / sizeof(wchar_t) : 0);
	return valid;
}
#endif
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
} // namespace
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CTextRef::CTextRef( const string & _fileName )
{
  fileName = _fileName;
  if ( !fileName.empty() && fileName[0] != '/' )
    fileName = "/" + fileName;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const wstring& CTextRef::GetText() const
{
	if ( fileName.empty() )
		return GetEmptyString();

	TTexts::const_iterator it = GetTextsCache().find( fileName );
	if ( it != GetTextsCache().end() )
		return it->second;

	wstring text;
	CObj<Stream> pStream = RootFileSystem::OpenFile( fileName, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING );
  if ( pStream && pStream->IsOk() )
	{
		int size = pStream->GetSize();
		if ( size < 2 )
		{
			systemLog( NLogg::LEVEL_WARNING ) << "Invalid text file '" << fileName << "' (must be UTF-16)!" << endl;
		}
		else
		{
			unsigned char sign[2];
			pStream->Read( sign, 2 );
			if ( sign[0] != 255 || sign[1] != 254 )
			{
				systemLog( NLogg::LEVEL_WARNING ) << "Invalid text file '" << fileName << "' (must be UTF-16)!" << endl;
			}
			else
			{
#if defined(__linux__)
				if (!ReadUtf16Payload(pStream, size - 2, &text))
					systemLog(NLogg::LEVEL_WARNING) << "Invalid UTF-16 text file '" << fileName << "'!" << endl;
#else
				int count = (size - 2) / 2;
				
				text.resize( count, 0 );
				pStream->Read( (void *)text.data(), count*2 );
#endif
			}
		}
	}

  pair<TTexts::iterator, bool> result = GetTextsCache().insert( make_pair<string, wstring>( fileName, text ) );
  return result.first->second;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CTextRef::DropCache()
{
  if ( fileName.empty() || isCacheReadonly )
		return;

	TTexts::iterator it = GetTextsCache().find( fileName );
	if ( it != GetTextsCache().end() )
		GetTextsCache().erase( it );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReloadTexts()
{
	GetTextsCache().clear();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void StoreTextsCache( const char* fileName, bool fullRead )
{
  if ( fullRead )
  {
    ReloadTexts();
    vector<string> folders;
    folders.push_back( "" );
    int index = 0;
    while ( index < folders.size() )
    {
      vector<string> temp;

      RootFileSystem::GetDirectories( &temp, folders[index] );
      for( vector<string>::const_iterator it = temp.begin(); it != temp.end(); ++it )
        folders.push_back( *it );

      RootFileSystem::GetFiles( &temp, folders[index], "*.txt", false );
      for( vector<string>::const_iterator it = temp.begin(); it != temp.end(); ++it )
      {
        CTextRef text( *it );
        text.GetText();
      }

      ++index;
    }
  }

	CObj<Stream> stream = new FileStream( fileName, FILEACCESS_WRITE, FILEOPEN_CREATE_NEW );
  NI_VERIFY( stream && stream->IsOk(), NI_STRFMT( "Cannot create file \"%s\"", fileName ), return );

  CObj<IBinSaver> saver = CreateWriteBinSaver( stream, 0, false );
  TTexts& texts = GetTextsCache();
  saver->Add( 1, &texts );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool TryLoadTextsCache( const char* fileName )
{
  CObj<Stream> stream = new FileStream( fileName, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING );
  if ( !stream || !stream->IsOk() )
    return false;

  CObj<IBinSaver> saver = CreateReadBinSaver( stream, 0 );
  TTexts& texts = GetTextsCache();
  saver->Add( 1, &texts );

  isCacheReadonly = true;
  return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
