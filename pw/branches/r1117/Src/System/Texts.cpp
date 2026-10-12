#include "stdafx.h"

#include "Texts.h"
#include "BinChunkSerializer.h"
#include "FileSystem/FileStream.h"
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
namespace
{
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static bool isCacheReadonly = false;
static string g_currentLanguage;
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
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
} // namespace
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
string GetLanguageShortCode()
{
  if ( !g_currentLanguage.empty() )
    return g_currentLanguage;

  wstring langW = NGlobal::GetVar("language").GetString();
  if ( langW.empty() )
    return string("ru");

  string lang;
  NStr::UnicodeToUTF8( &lang, langW );
  g_currentLanguage = lang.substr( 0, lang.find( '-' ) );
  return g_currentLanguage;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SetCurrentLanguage( const string & language )
{
  g_currentLanguage = language.substr( 0, language.find( '-' ) );
  ReloadTexts();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
string GetLocalizedFileName( const string & fileName )
{
  if ( fileName.empty() )
    return fileName;

  // ru is the original language: its texts live in files without a suffix
  string langCode = GetLanguageShortCode();
  if ( langCode.empty() || langCode == "ru" )
    return fileName;

  int len = (int)fileName.size();
  if ( len < 5 || fileName[len - 4] != '.' || fileName[len - 3] != 't' || fileName[len - 2] != 'x' || fileName[len - 1] != 't' )
    return fileName;

  string localized = fileName.substr( 0, len - 4 );
  localized += "_";
  localized += langCode;
  localized += ".txt";
  return localized;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CTextRef::CTextRef( const string & _fileName )
{
  fileName = _fileName;
  if ( !fileName.empty() && fileName[0] != '/' )
    fileName = "/" + fileName;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static bool LoadTextFile( const string & path, wstring & text )
{
  CObj<Stream> pStream = RootFileSystem::OpenFile( path, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING );
  if ( !pStream || !pStream->IsOk() )
    return false;

  int size = pStream->GetSize();
  if ( size < 2 )
  {
    systemLog( NLogg::LEVEL_WARNING ) << "Invalid text file '" << path << "' (must be UTF-16)!" << endl;
    return false;
  }

  unsigned char sign[2];
  pStream->Read( sign, 2 );
  if ( sign[0] != 255 || sign[1] != 254 )
  {
    systemLog( NLogg::LEVEL_WARNING ) << "Invalid text file '" << path << "' (must be UTF-16)!" << endl;
    return false;
  }

  int count = (size - 2) / 2;
  text.resize( count, 0 );
  pStream->Read( (void *)text.data(), count * 2 );
  return true;
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
  string localizedFileName = GetLocalizedFileName( fileName );

  if ( !localizedFileName.empty() && localizedFileName != fileName )
  {
    if ( LoadTextFile( localizedFileName, text ) )
    {
      pair<TTexts::iterator, bool> result = GetTextsCache().insert( make_pair<string, wstring>( fileName, text ) );
      return result.first->second;
    }
  }

  LoadTextFile( fileName, text );

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
