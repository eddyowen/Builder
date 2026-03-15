/*
	===========================================================================

	Builder

	Copyright (c) 2025 Dan Moody

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	SOFTWARE.

	===========================================================================
*/

#include "builder_local.h"
#include "core/include/allocation_context.h"
#include "core/include/array.inl"
#include "core/include/core_types.h"
#include "core/include/debug.h"
#include "core/include/string_builder.h"
#include "core/include/string_helpers.h"
#include "core/include/file.h"
#include "core/include/paths.h"

#include <string>

#ifdef _WIN32
#include <sstream>
#include <algorithm>
#include <unordered_set>
#endif

namespace {
	static const char* DefaultIncludeFilter = "*.*";
	static const char* DefaultExcludeFilter = "*.user,.git,.vs,.cache,.builder,bin,intermediate,bin";
}

constexpr std::string_view BoolToString(const bool8 value)
{
	return value ? "true" : "false";
}

#ifdef _WIN32
struct FileDataBuffer {
	Array<u8>	data;
	u64			readOffset;
};

struct WindowsSDK {
    std::string path;
    std::string version;
};

static std::vector<int> ParseWindowsSDKVersion(const std::string& version) {
    std::vector<int> parts;
    std::stringstream ss(version);
    std::string part;
    while (std::getline(ss, part, '.'))
        parts.push_back(std::stoi(part));
    return parts;
}

static bool IsNewerVersion(const WindowsSDK& a, const WindowsSDK& b) {
    auto partsA = ParseWindowsSDKVersion(a.version);
    auto partsB = ParseWindowsSDKVersion(b.version);
    return partsA > partsB;
}
#endif

static std::vector<WindowsSDK> GetInstalledSDKs() {
#ifdef _WIN32
    HKEY rootKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, R"(SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots)", 0, KEY_READ, &rootKey) != ERROR_SUCCESS)
        return {};

    // Read the root path ONCE from the root key
    char rootPath[MAX_PATH];
    DWORD pathSize = sizeof(rootPath);

	// No SDK root found
    if (RegQueryValueExA(rootKey, "KitsRoot10", nullptr, nullptr, (LPBYTE)rootPath, &pathSize) != ERROR_SUCCESS) {
        RegCloseKey(rootKey);
        return {};
    }

    std::vector<WindowsSDK> sdks;
    char versionName[64];
    DWORD index = 0, nameSize = sizeof(versionName);

    while (RegEnumKeyExA(rootKey, index++, versionName, &nameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        sdks.push_back({ rootPath, versionName });
        nameSize = sizeof(versionName);
    }

    RegCloseKey(rootKey);
    return sdks;
#else
	return {};
#endif
}

static std::string GetVisualStudioInstallationPath()
{
	std::string vsInstallationPath;
#ifdef _WIN32
	String vswhereStdout;

	Array<const char *> args;
	args.add( "C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe" );
	args.add( "-latest" );
	args.add( "-property" );
	args.add( "installationPath" );
	s32 exitCode = RunProc( &args, NULL, 0, &vswhereStdout );

	// fail test if vswhere errors
	if ( exitCode != 0 ) {
		return vsInstallationPath;
	}
	
	if ( string_ends_with( vswhereStdout.data, "\r\n" ) ) {
		string_substring(vswhereStdout.data, 0, strlen(vswhereStdout.data) - (strlen("\r\n") - 1), vswhereStdout.data );
	}

	vsInstallationPath = vswhereStdout.data;
#endif
	return vsInstallationPath;
}

bool8 Generate10xWorkspace( buildContext_t *context, BuilderOptions *options ) {
	assert( context );
	assert( context->inputFile );
	assert( context->inputFilePath.data );
	assert( options );

	const Ten10xWorkspace& workspace = options->tenXWorkspace;

	const std::string& outputPath 		= workspace.outputPath;
	const std::string& includeFilter 	= workspace.includeFilter.empty() ? DefaultIncludeFilter : workspace.includeFilter;
	const std::string& excludeFilter 	= workspace.excludeFilter.empty() ? DefaultExcludeFilter : workspace.excludeFilter;

	const bool8 isFolder 				= workspace.isFolder;
	const bool8 includeFilesWithoutExt 	= workspace.includeFilesWithoutExt;
	const bool8 syncFiles 				= workspace.syncFiles;
	const bool8 recursive 				= workspace.recursive;
	const bool8 showEmptyFolders 		= workspace.showEmptyFolders;
	const bool8 useVisualStudioEnvBat 	= workspace.useVisualStudioEnvBat;
	const bool8 captureExeOutput 		= workspace.captureExeOutput;

	const char *workspacePath = NULL;
	const char* inputFilePath = context->inputFilePath.data;
	if ( !outputPath.c_str() ) {
		workspacePath = tprintf( "%s%c%s%stest.10x", inputFilePath, PATH_SEPARATOR, outputPath.c_str(), PATH_SEPARATOR );
	} else {
		workspacePath = tprintf( "%s%cstest.10x", inputFilePath, PATH_SEPARATOR );
	}

	StringBuilder workspaceContent = {};
	string_builder_reset( &workspaceContent );
	defer( string_builder_destroy( &workspaceContent ) );

	string_builder_appendf( &workspaceContent, "<?xml version=\"1.0\"?>\n" );
	string_builder_appendf( &workspaceContent, "<N10X>\n" );
	string_builder_appendf( &workspaceContent, "\t<Workspace>\n" );

	// ===============================================================================================================
	// Filters
	// ===============================================================================================================

	string_builder_appendf( &workspaceContent, "\t\t<IncludeFilter>%s</IncludeFilter>\n", includeFilter.c_str() );
	string_builder_appendf( &workspaceContent, "\t\t<ExcludeFilter>%s</ExcludeFilter>\n", excludeFilter.c_str() );

	string_builder_appendf( &workspaceContent, "\t\t<IsFolder>%s</IsFolder>\n", 							BoolToString( isFolder ).data() );
	string_builder_appendf( &workspaceContent, "\t\t<IncludeFilesWithoutExt>%s</IncludeFilesWithoutExt>\n", BoolToString( includeFilesWithoutExt ).data() );
	string_builder_appendf( &workspaceContent, "\t\t<SyncFiles>%s</SyncFiles>\n", 							BoolToString( syncFiles ).data() );
	string_builder_appendf( &workspaceContent, "\t\t<Recursive>%s</Recursive>\n", 							BoolToString( recursive ).data() );
	string_builder_appendf( &workspaceContent, "\t\t<ShowEmptyFolders>%s</ShowEmptyFolders>\n", 			BoolToString( showEmptyFolders ).data() );
	string_builder_appendf( &workspaceContent, "\t\t<UseVisualStudioEnvBat>%s</UseVisualStudioEnvBat>\n", 	BoolToString( useVisualStudioEnvBat ).data() );
	string_builder_appendf( &workspaceContent, "\t\t<CaptureExeOutput>%s</CaptureExeOutput>\n", 			BoolToString( captureExeOutput ).data() );

	// ===============================================================================================================
	// Configs
	// ===============================================================================================================

	string_builder_appendf( &workspaceContent, "\t\t<Configurations>\n" );
	For (u64, configIndex, 0, options->configs.size()) {
		BuildConfig& config = options->configs[configIndex];
		string_builder_appendf( &workspaceContent, "\t\t\t<Configuration>" );
		string_builder_appendf( &workspaceContent, config.name.c_str() );
		string_builder_appendf( &workspaceContent, "</Configuration>\n" );


	}
	string_builder_appendf( &workspaceContent, "\t\t</Configurations>\n" );

	// ===============================================================================================================
	// Platforms
	// ===============================================================================================================

	const std::vector<std::string>& platforms = workspace.platforms;
	string_builder_appendf( &workspaceContent, "\t\t<Platforms>\n" );
	// @NOTE-Ed - Default to x64 if not platforms provided
	if (platforms.size() == 0) {
		string_builder_appendf( &workspaceContent, "\t\t\t<Platform>x64</Platform>\n" );
	} else {
		For (u64, platformIndex, 0, platforms.size()) {
			string_builder_appendf( &workspaceContent, "\t\t\t<Platform>" );
			string_builder_appendf( &workspaceContent, platforms[platformIndex].c_str() );
			string_builder_appendf( &workspaceContent, "</Platform>\n" );
		}
	}
	string_builder_appendf( &workspaceContent, "\t\t</Platforms>\n" );

	// ===============================================================================================================
	// Additional Includes
	// ===============================================================================================================

	string_builder_appendf( &workspaceContent, "\t\t<AdditionalIncludePaths>\n" );

	const std::string vsInstallationPath = GetVisualStudioInstallationPath();
	if ( !vsInstallationPath.empty() ) {
		char* fileContent;
		u64 fileLength;

		const char* vsToolsVersionRelativePath = R"(VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt)";
		const char* toolsVersionFilename = tprintf( "%s%c%s", vsInstallationPath.c_str(), PATH_SEPARATOR, vsToolsVersionRelativePath );

		if ( file_read_entire( toolsVersionFilename, &fileContent, &fileLength, true ) ) {
			// Safe as VCToolsVersion.default.txt always contains only 1 entry
			std::string msvcVersion = std::string( fileContent ).substr( 0, strlen( fileContent ) - strlen( "\r\n" ) );

			string_builder_appendf( 
				&workspaceContent, 
				"\t\t\t<AdditionalIncludePath>%s%c%s%c%s%c%s</AdditionalIncludePath>\n", 
				vsInstallationPath.c_str(), 
				PATH_SEPARATOR, 
				path_canonicalise("VC/Tools/MSVC"), 
				PATH_SEPARATOR, 
				msvcVersion.c_str(), 
				PATH_SEPARATOR, 
				"include"
			);

			string_builder_appendf( &workspaceContent, "\t\t\t<AdditionalIncludePath>%s%c%s</AdditionalIncludePath>\n", vsInstallationPath.c_str(), PATH_SEPARATOR, path_canonicalise( "VC/Auxiliary/VS/include" ) );
		}
	}

	// Windows SDK include paths (if any)
	std::vector<WindowsSDK> windowsSDKs = GetInstalledSDKs();
	if (windowsSDKs.size() > 0 ) {
		std::sort(windowsSDKs.begin(), windowsSDKs.end(), IsNewerVersion);
		// Use the latest installation only
		const WindowsSDK& sdk = windowsSDKs[0];
		const char* sdkPath = sdk.path.c_str();
		const char* sdkVersion = sdk.version.c_str();
		
		const char* additionalIncludeFmt = "\t\t\t<AdditionalIncludePath>%s%s%c%s%c%s</AdditionalIncludePath>\n";
		// sdk.path.c_str() here already contains the trailing "\", so no PATH_SEPARATOR needed
		string_builder_appendf( &workspaceContent, additionalIncludeFmt, sdkPath, "include", PATH_SEPARATOR, sdkVersion, PATH_SEPARATOR, "ucrt" );
		string_builder_appendf( &workspaceContent, additionalIncludeFmt, sdkPath, "include", PATH_SEPARATOR, sdkVersion, PATH_SEPARATOR, "um" );
		string_builder_appendf( &workspaceContent, additionalIncludeFmt, sdkPath, "include", PATH_SEPARATOR, sdkVersion, PATH_SEPARATOR, "shared" );
		string_builder_appendf( &workspaceContent, additionalIncludeFmt, sdkPath, "include", PATH_SEPARATOR, sdkVersion, PATH_SEPARATOR, "cppwinrt" );
	}

	const char* builderIncludePath = tprintf( "%s%c..%cinclude", path_remove_file_from_path( path_app_path() ), PATH_SEPARATOR, PATH_SEPARATOR );
	string_builder_appendf( &workspaceContent, "\t\t\t<AdditionalIncludePath>%s</AdditionalIncludePath>\n", builderIncludePath );
	
	const char* builderClangIncludePath = tprintf( "%s%c..%cclang%cinclude", path_remove_file_from_path( path_app_path() ), PATH_SEPARATOR, PATH_SEPARATOR, PATH_SEPARATOR );
	string_builder_appendf( &workspaceContent, "\t\t\t<AdditionalIncludePath>%s</AdditionalIncludePath>\n", builderClangIncludePath );

	std::unordered_set<std::string> uniqueIncludes;
	For (u64, configIndex, 0, options->configs.size()) {
		BuildConfig& config = options->configs[configIndex];
		const std::vector<std::string>& additionalIncludes = config.additionalIncludes; 

		For (u64, includeIndex, 0, additionalIncludes.size()) {
			const std::string& include = additionalIncludes[includeIndex];
			uniqueIncludes.insert( include );
		}
	}

	for ( const std::string& include : uniqueIncludes ) {
		string_builder_appendf( &workspaceContent, "\t\t\t<AdditionalIncludePath>%s</AdditionalIncludePath>\n", include.c_str() );
	}

	string_builder_appendf( &workspaceContent, "\t\t</AdditionalIncludePaths>\n" );

	// ===============================================================================================================
	// Additional Includes
	// ===============================================================================================================

	string_builder_appendf( &workspaceContent, "\t\t<Defines>\n" );

	std::unordered_set<std::string> uniqueDefines;
	For (u64, configIndex, 0, options->configs.size()) {
		BuildConfig& config = options->configs[configIndex];
		const std::vector<std::string>& defines = config.defines; 

		For (u64, defineIndex, 0, defines.size()) {
			const std::string& define = defines[defineIndex];
			uniqueDefines.insert( define );
		}
	}

	for ( const std::string& define : uniqueDefines ) {
		string_builder_appendf( &workspaceContent, "\t\t\t<Define>%s</Define>\n", define.c_str() );
	}

	string_builder_appendf( &workspaceContent, "\t\t</Defines>\n" );

	string_builder_appendf( &workspaceContent, "\t</Workspace>\n" );
	string_builder_appendf( &workspaceContent, "</N10X>" );

	const char *content = string_builder_to_string( &workspaceContent );
	const u64 contentLength = strlen( content );

	bool8 written = file_write_entire( workspacePath, content, contentLength );
	if ( !written ) {
		errorCode_t errorCode = get_last_error_code();
		error( "Failed to write \"%s\": " ERROR_CODE_FORMAT ".\n", workspacePath, errorCode );

		return false;
	}

	options->configs.clear();
	return true;
}
