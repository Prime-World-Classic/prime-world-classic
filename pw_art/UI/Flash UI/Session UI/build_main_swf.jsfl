// Builds main.swf of the combat HUD from main.fla + classes in Flash CS6 without the UI:
//   "D:\AdobeFlashCS6\Adobe Flash CS6\Flash.exe" "<this folder>\build_main_swf.jsfl"
// Result: main.swf next to main.fla and a copy in pw/branches/r1117/Data/UI/Screens/Combat/Flash/main.swf;
// compiler output: build_main_swf.log, errors: build_main_swf_errors.log. Flash quits when done.
var here = fl.scriptURI.substr(0, fl.scriptURI.lastIndexOf("/") + 1);
var swf = here + "main.swf";
var doc = fl.openDocument(here + "main.fla");
fl.outputPanel.clear();
doc.exportSWF(swf, true);
fl.outputPanel.save(here + "build_main_swf.log");
try { fl.compilerErrors.save(here + "build_main_swf_errors.log"); } catch (e) {}
doc.close(false);
// pw_art/UI/Flash UI/Session UI/ -> pw/branches/r1117/Data/UI/Screens/Combat/Flash/
var root = here.substr(0, here.lastIndexOf("/pw_art/") + 1);
FLfile.copy(swf, root + "pw/branches/r1117/Data/UI/Screens/Combat/Flash/main_new.swf.tmp");
FLfile.remove(root + "pw/branches/r1117/Data/UI/Screens/Combat/Flash/main.swf");
FLfile.copy(root + "pw/branches/r1117/Data/UI/Screens/Combat/Flash/main_new.swf.tmp", root + "pw/branches/r1117/Data/UI/Screens/Combat/Flash/main.swf");
FLfile.remove(root + "pw/branches/r1117/Data/UI/Screens/Combat/Flash/main_new.swf.tmp");
fl.quit(false);
