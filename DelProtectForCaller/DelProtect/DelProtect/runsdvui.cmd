cd /d "G:\Code\VSProj\DelProtect\DelProtect" & msbuild "DelProtect.vcxproj" /t:sdv /p:inputs="/clean /devenv" /p:configuration="Release" /p:platform="x64" /p:SolutionDir="G:\Code\VSProj\DelProtect" 
exit %errorlevel% 