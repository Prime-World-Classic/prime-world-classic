import re

path_h = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/LogStreamBuffer.h"
with open(path_h, "r", encoding="cp1251") as f:
    text_h = f.read()

# Replace constructor with declaration
text_h = re.sub(
    r'(StreamBuffer\( CChannelLogger & _logger, const SEntryInfo & _info \)) :\n\s*logger\( _logger \),\n\s*entryInfo\( _info \),\n\s*textStart\( Buffer\(\)\.c_str\(\) \)\n\s*\{\n\s*WriteHeader\( logger\.HeaderFormat\(\) \);\n\s*textStart = Buffer\(\)\.c_str\(\) \+ Buffer\(\)\.Length\(\);\n\s*\}',
    r'\1;', text_h
)

# Replace destructor with declaration
text_h = re.sub(
    r'(~StreamBuffer\(\)) \{\n\s*Push\( "\\r\\n" \);\n\s*logger\.Log\( entryInfo, Buffer\(\)\.c_str\(\), textStart \);\n\s*\}',
    r'\1;', text_h
)

with open(path_h, "w", encoding="cp1251") as f:
    f.write(text_h)

path_cpp = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/SystemLog.cpp"
with open(path_cpp, "r", encoding="cp1251") as f:
    text_cpp = f.read()

cpp_add = """
namespace NLogg {
  StreamBuffer::StreamBuffer( CChannelLogger & _logger, const SEntryInfo & _info ) :
  logger( _logger ),
  entryInfo( _info ),
  textStart( Buffer().c_str() )
  {
    WriteHeader( logger.HeaderFormat() );
    textStart = Buffer().c_str() + Buffer().Length();
  }

  StreamBuffer::~StreamBuffer() {
    Push( "\\r\\n" );
    logger.Log( entryInfo, Buffer().c_str(), textStart );
  }
}
"""

with open(path_cpp, "w", encoding="cp1251") as f:
    f.write(text_cpp + cpp_add)

