#!/bin/bash
FILE="/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PF_GameLogic/TutorialSplash.cpp"

sed -i 's/namespace/  \n#ifdef _WIN32\nnamespace/' $FILE
sed -i 's/REGISTER_VAR/#endif\n\nREGISTER_VAR/' $FILE
sed -i 's/TutorialSplash::TutorialSplash()/#else\n  TutorialSplash::TutorialSplash() : thread(NULL) {}\n  TutorialSplash::~TutorialSplash() {}\n#endif\n#ifdef _WIN32\n  TutorialSplash::TutorialSplash()/' $FILE
sed -i '$a#endif' $FILE

