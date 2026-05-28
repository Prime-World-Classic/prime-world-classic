import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

import re
content = re.sub(r'else \{ gl_Position = vec4\(\(position\.x\/1366\.0\)\*2\.0-1\.0, 1\.0-\(position\.y\/768\.0\)\*2\.0, 0\.0, 1\.0\); \}',
                 'else { gl_Position = vec4((position.x/1366.0)*2.0-1.0, 1.0-(position.y/768.0)*2.0, 0.0, 1.0); }', content)

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
