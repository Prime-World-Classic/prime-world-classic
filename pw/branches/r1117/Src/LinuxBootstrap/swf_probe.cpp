#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <zlib.h>

namespace fs = std::filesystem;

namespace
{
struct SwfTagSummary
{
  size_t total = 0;
  size_t doAction = 0;
  size_t doInitAction = 0;
  size_t doAbc = 0;
  size_t fileAttributes = 0;
  size_t symbolClass = 0;
  size_t exportAssets = 0;
  size_t importAssets = 0;
  size_t defineSprite = 0;
  size_t defineShape = 0;
  size_t defineMorphShape = 0;
  size_t defineBitmap = 0;
  size_t defineFont = 0;
  size_t defineText = 0;
  size_t defineEditText = 0;
  size_t defineButton = 0;
  size_t defineSound = 0;
  size_t video = 0;
  size_t scaleGrid = 0;
  size_t binaryData = 0;
  size_t unsupportedKnown = 0;
};

struct SwfProbeResult
{
  fs::path path;
  bool ok = false;
  bool compressed = false;
  bool zwsCompressed = false;
  bool declaredLengthMismatch = false;
  bool parseTruncated = false;
  bool as3FileAttributes = false;
  bool hasFscommandText = false;
  bool hasExternalInterfaceText = false;
  bool hasLoaderWindowInterfaceText = false;
  bool hasPreferencesInterfaceText = false;
  unsigned int version = 0;
  unsigned int declaredLength = 0;
  unsigned int expandedLength = 0;
  unsigned int frameRate = 0;
  unsigned int frameCount = 0;
  std::string error;
  SwfTagSummary tags;
};

uint16_t ReadLe16(const std::vector<unsigned char>& data, size_t pos)
{
  return static_cast<uint16_t>(data[pos]) |
    static_cast<uint16_t>(data[pos + 1] << 8);
}

uint32_t ReadLe32(const std::vector<unsigned char>& data, size_t pos)
{
  return static_cast<uint32_t>(data[pos]) |
    (static_cast<uint32_t>(data[pos + 1]) << 8) |
    (static_cast<uint32_t>(data[pos + 2]) << 16) |
    (static_cast<uint32_t>(data[pos + 3]) << 24);
}

bool ReadFile(const fs::path& path, std::vector<unsigned char>* out, std::string* error)
{
  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    if (error)
      *error = "open failed";
    return false;
  }

  file.seekg(0, std::ios::end);
  const std::streamoff size = file.tellg();
  if (size < 0)
  {
    if (error)
      *error = "size failed";
    return false;
  }

  file.seekg(0, std::ios::beg);
  out->resize(static_cast<size_t>(size));
  if (!out->empty())
  {
    file.read(reinterpret_cast<char*>(&(*out)[0]), size);
    if (!file)
    {
      if (error)
        *error = "read failed";
      return false;
    }
  }

  return true;
}

bool InflateSwfBody(
  const std::vector<unsigned char>& source,
  uint32_t declaredLength,
  std::vector<unsigned char>* body,
  std::string* error)
{
  if (declaredLength < 8)
  {
    if (error)
      *error = "bad declared length";
    return false;
  }

  body->assign(declaredLength - 8, 0);
  uLongf outputSize = static_cast<uLongf>(body->size());
  const int result = uncompress(
    reinterpret_cast<Bytef*>(body->data()),
    &outputSize,
    reinterpret_cast<const Bytef*>(&source[8]),
    static_cast<uLong>(source.size() - 8));
  if (result != Z_OK)
  {
    if (error)
      *error = "zlib inflate failed";
    return false;
  }

  body->resize(static_cast<size_t>(outputSize));
  return true;
}

bool ContainsAsciiNoCase(const std::vector<unsigned char>& data, const char* needle)
{
  const size_t needleLength = std::strlen(needle);
  if (!needleLength || data.size() < needleLength)
    return false;

  for (size_t i = 0; i + needleLength <= data.size(); ++i)
  {
    bool found = true;
    for (size_t j = 0; j < needleLength; ++j)
    {
      if (std::tolower(data[i + j]) != std::tolower(static_cast<unsigned char>(needle[j])))
      {
        found = false;
        break;
      }
    }
    if (found)
      return true;
  }

  return false;
}

size_t SkipSwfRect(const std::vector<unsigned char>& body)
{
  if (body.empty())
    return 0;

  const unsigned int nbits = body[0] >> 3;
  const size_t bits = 5 + static_cast<size_t>(nbits) * 4;
  return (bits + 7) / 8;
}

void CountKnownTag(uint16_t tagType, SwfTagSummary* summary)
{
  switch (tagType)
  {
    case 2:
    case 22:
    case 32:
    case 83:
      ++summary->defineShape;
      break;
    case 6:
    case 20:
    case 21:
    case 35:
    case 36:
    case 90:
      ++summary->defineBitmap;
      break;
    case 7:
    case 34:
      ++summary->defineButton;
      break;
    case 10:
    case 48:
    case 73:
    case 75:
    case 88:
    case 91:
      ++summary->defineFont;
      break;
    case 11:
    case 33:
      ++summary->defineText;
      break;
    case 12:
      ++summary->doAction;
      break;
    case 14:
    case 15:
    case 17:
    case 18:
    case 19:
    case 45:
    case 89:
      ++summary->defineSound;
      break;
    case 37:
      ++summary->defineEditText;
      break;
    case 39:
      ++summary->defineSprite;
      break;
    case 46:
    case 84:
      ++summary->defineMorphShape;
      break;
    case 56:
      ++summary->exportAssets;
      break;
    case 57:
    case 71:
      ++summary->importAssets;
      break;
    case 59:
      ++summary->doInitAction;
      break;
    case 60:
    case 61:
      ++summary->video;
      break;
    case 69:
      ++summary->fileAttributes;
      break;
    case 76:
      ++summary->symbolClass;
      break;
    case 78:
      ++summary->scaleGrid;
      break;
    case 82:
      ++summary->doAbc;
      break;
    case 87:
      ++summary->binaryData;
      break;
    case 24:
    case 58:
    case 63:
    case 64:
    case 65:
    case 66:
    case 74:
    case 77:
    case 86:
      ++summary->unsupportedKnown;
      break;
    default:
      break;
  }
}

void ParseTags(const std::vector<unsigned char>& body, SwfProbeResult* result)
{
  size_t pos = SkipSwfRect(body);
  if (pos + 4 > body.size())
  {
    result->parseTruncated = true;
    return;
  }

  result->frameRate = ReadLe16(body, pos) / 256;
  pos += 2;
  result->frameCount = ReadLe16(body, pos);
  pos += 2;

  while (pos + 2 <= body.size())
  {
    const uint16_t tagHeader = ReadLe16(body, pos);
    pos += 2;

    const uint16_t tagType = tagHeader >> 6;
    uint32_t tagLength = tagHeader & 0x3f;
    if (tagLength == 0x3f)
    {
      if (pos + 4 > body.size())
      {
        result->parseTruncated = true;
        return;
      }
      tagLength = ReadLe32(body, pos);
      pos += 4;
    }

    if (pos + tagLength > body.size())
    {
      result->parseTruncated = true;
      return;
    }

    ++result->tags.total;
    CountKnownTag(tagType, &result->tags);
    if (tagType == 69 && tagLength >= 4)
    {
      const uint32_t flags = ReadLe32(body, pos);
      result->as3FileAttributes = result->as3FileAttributes || ((flags & 0x08) != 0);
    }

    pos += tagLength;
    if (tagType == 0)
      break;
  }
}

SwfProbeResult ProbeSwf(const fs::path& path)
{
  SwfProbeResult result;
  result.path = path;

  std::vector<unsigned char> fileData;
  if (!ReadFile(path, &fileData, &result.error))
    return result;

  if (fileData.size() < 8)
  {
    result.error = "too small";
    return result;
  }

  const char signature0 = static_cast<char>(fileData[0]);
  const char signature1 = static_cast<char>(fileData[1]);
  const char signature2 = static_cast<char>(fileData[2]);
  if (signature1 != 'W' || signature2 != 'S' ||
      (signature0 != 'F' && signature0 != 'C' && signature0 != 'Z'))
  {
    result.error = "not a SWF";
    return result;
  }

  result.version = fileData[3];
  result.declaredLength = ReadLe32(fileData, 4);
  result.compressed = signature0 == 'C';
  result.zwsCompressed = signature0 == 'Z';

  std::vector<unsigned char> body;
  if (result.zwsCompressed)
  {
    result.error = "ZWS/LZMA compression is not parsed by this probe";
    return result;
  }
  else if (result.compressed)
  {
    if (!InflateSwfBody(fileData, result.declaredLength, &body, &result.error))
      return result;
  }
  else
  {
    if (result.declaredLength != fileData.size())
      result.declaredLengthMismatch = true;
    if (fileData.size() > 8)
      body.assign(fileData.begin() + 8, fileData.end());
  }

  result.expandedLength = static_cast<unsigned int>(body.size() + 8);
  if (result.expandedLength != result.declaredLength)
    result.declaredLengthMismatch = true;

  result.hasFscommandText = ContainsAsciiNoCase(body, "fscommand");
  result.hasExternalInterfaceText = ContainsAsciiNoCase(body, "ExternalInterface");
  result.hasLoaderWindowInterfaceText = ContainsAsciiNoCase(body, "LoaderWindowInterface");
  result.hasPreferencesInterfaceText = ContainsAsciiNoCase(body, "PreferencesInterface");

  ParseTags(body, &result);
  result.ok = !result.parseTruncated;
  if (!result.ok && result.error.empty())
    result.error = "tag parse truncated";

  return result;
}

bool IsSwfPath(const fs::path& path)
{
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension == ".swf" || extension == ".gfx";
}

std::string MakeRelativePath(const fs::path& root, const fs::path& path)
{
  std::error_code ec;
  fs::path relative = fs::relative(path, root, ec);
  if (ec)
    return path.string();
  return relative.string();
}

std::string ResolveRuffleClass(const SwfProbeResult& result)
{
  if (!result.ok)
    return "unparsed";
  if (result.as3FileAttributes || result.tags.doAbc > 0)
    return "needs-avm2-as3";
  if (result.tags.doAction > 0 || result.tags.doInitAction > 0)
    return "avm1-as1-as2";
  return "timeline-only";
}

void PrintResult(const fs::path& root, const SwfProbeResult& result)
{
  std::printf(
    "SWF %s status=%s version=%u compression=%s declared=%u expanded=%u frames=%u fps=%u ruffle=%s\n",
    MakeRelativePath(root, result.path).c_str(),
    result.ok ? "ok" : "failed",
    result.version,
    result.zwsCompressed ? "ZWS" : (result.compressed ? "CWS" : "FWS"),
    result.declaredLength,
    result.expandedLength,
    result.frameCount,
    result.frameRate,
    ResolveRuffleClass(result).c_str());

  if (!result.ok)
  {
    std::printf("  error=%s\n", result.error.c_str());
    return;
  }

  std::printf(
    "  tags total=%zu as3=%s doabc=%zu doaction=%zu doinit=%zu symbols=%zu import=%zu export=%zu sprite=%zu shape=%zu morph=%zu bitmap=%zu font=%zu text=%zu edit=%zu button=%zu sound=%zu video=%zu scale9=%zu binary=%zu\n",
    result.tags.total,
    (result.as3FileAttributes || result.tags.doAbc > 0) ? "yes" : "no",
    result.tags.doAbc,
    result.tags.doAction,
    result.tags.doInitAction,
    result.tags.symbolClass,
    result.tags.importAssets,
    result.tags.exportAssets,
    result.tags.defineSprite,
    result.tags.defineShape,
    result.tags.defineMorphShape,
    result.tags.defineBitmap,
    result.tags.defineFont,
    result.tags.defineText,
    result.tags.defineEditText,
    result.tags.defineButton,
    result.tags.defineSound,
    result.tags.video,
    result.tags.scaleGrid,
    result.tags.binaryData);

  std::printf(
    "  callbacks fscommand=%s externalInterface=%s loaderInterface=%s preferencesInterface=%s mismatch=%s truncated=%s\n",
    result.hasFscommandText ? "yes" : "no",
    result.hasExternalInterfaceText ? "yes" : "no",
    result.hasLoaderWindowInterfaceText ? "yes" : "no",
    result.hasPreferencesInterfaceText ? "yes" : "no",
    result.declaredLengthMismatch ? "yes" : "no",
    result.parseTruncated ? "yes" : "no");
}

void AddSwfPathsFromRoot(const fs::path& root, std::vector<fs::path>* swfs)
{
  std::error_code ec;
  if (!fs::exists(root, ec))
    return;

  if (fs::is_regular_file(root, ec))
  {
    if (IsSwfPath(root))
      swfs->push_back(root);
    return;
  }

  for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator();
       it.increment(ec))
  {
    if (!fs::is_regular_file(*it, ec))
      continue;
    if (IsSwfPath(it->path()))
      swfs->push_back(it->path());
  }
}
}

int main(int argc, char** argv)
{
  std::vector<fs::path> roots;
  if (argc > 1)
  {
    for (int i = 1; i < argc; ++i)
      roots.push_back(argv[i]);
  }
  else
  {
    roots.push_back(fs::current_path());
  }

  std::vector<fs::path> swfs;
  for (size_t i = 0; i < roots.size(); ++i)
    AddSwfPathsFromRoot(roots[i], &swfs);

  std::sort(swfs.begin(), swfs.end());
  swfs.erase(std::unique(swfs.begin(), swfs.end()), swfs.end());

  size_t parsed = 0;
  size_t compressed = 0;
  size_t as3 = 0;
  size_t avm1 = 0;
  size_t externalInterface = 0;
  size_t fscommand = 0;
  size_t bitmapHeavy = 0;
  size_t textHeavy = 0;
  size_t video = 0;
  size_t failed = 0;

  std::printf("PrimeWorldLinuxSwfProbe files=%zu\n", swfs.size());
  for (size_t i = 0; i < swfs.size(); ++i)
  {
    const fs::path printRoot = roots.empty() ? fs::current_path() : roots.front();
    const SwfProbeResult result = ProbeSwf(swfs[i]);
    PrintResult(printRoot, result);
    if (!result.ok)
    {
      ++failed;
      continue;
    }

    ++parsed;
    if (result.compressed)
      ++compressed;
    if (result.as3FileAttributes || result.tags.doAbc > 0)
      ++as3;
    if (result.tags.doAction > 0 || result.tags.doInitAction > 0)
      ++avm1;
    if (result.hasExternalInterfaceText)
      ++externalInterface;
    if (result.hasFscommandText)
      ++fscommand;
    if (result.tags.defineBitmap >= 10)
      ++bitmapHeavy;
    if (result.tags.defineText + result.tags.defineEditText >= 10)
      ++textHeavy;
    if (result.tags.video > 0)
      ++video;
  }

  std::printf(
    "Summary parsed=%zu failed=%zu compressed=%zu as3=%zu avm1=%zu externalInterface=%zu fscommand=%zu bitmapHeavy=%zu textHeavy=%zu video=%zu\n",
    parsed,
    failed,
    compressed,
    as3,
    avm1,
    externalInterface,
    fscommand,
    bitmapHeavy,
    textHeavy,
    video);

  if (as3 > 0)
    std::printf("Ruffle note: AS3 movies require AVM2 support; test these first with Ruffle before replacing Linux FlashContainer2.\n");
  if (externalInterface > 0 || fscommand > 0)
    std::printf("Ruffle note: callback-bearing movies need a Linux adapter for ExternalInterface/fscommand into the existing FlashInterface contract.\n");

  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
