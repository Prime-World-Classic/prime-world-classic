#pragma once

class CommandLine
{
public:
  CommandLine()  : isClean(false), isOutDir(false), isShader(false), isNoShare(false) {}
  
  void Print();
  void Parse(int argc, char *argv[ ]);
  
  int GetCleanState() const;
  const char* GetOutDir() const;
  const char* GetShaderName() const;

  bool IsNoShare() const { return isNoShare; }

private:
  bool isClean;

  string outDir;
  bool isOutDir;

  string shader;
  bool isShader;

  bool isNoShare;
};

