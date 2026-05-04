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

#ifdef _WIN32
#include "win_support.h"
#include "core/include/debug.h"
#include "core/include/allocation_context.h"
#include "core/include/array.inl"
#include "core/include/core_types.h"
#include "core/include/string_builder.h"
#include "core/include/string_helpers.h"
#include "core/include/file.h"
#include "core/include/paths.h"

#include <string>
#include <functional>
#include <algorithm>

#include <sstream>
#include <unordered_set>

namespace {
	static const char* DefaultIncludeFilter = "*.*";
	static const char* DefaultExcludeFilter = "*.user,.git,.vs,.cache,.builder,bin,intermediate,bin";
}

constexpr std::string_view BoolToString( const bool8 value )
{
	return value ? "true" : "false";
}

static const std::vector<std::string> GetUserCompilerDefines( const TenxWorkspaceOptions& workspace, Compiler compiler ) {
	const std::vector<TenxCompiler>& compilers = workspace.compilers;
	auto it = std::find_if( compilers.begin(), compilers.end(), [&compiler]( const TenxCompiler& lhs ) {
		return lhs.id == compiler;
	});
	
	if ( it != compilers.end() ) {
		return it->defines;
	}
	
	return {};
}
#endif

bool8 GenerateTenxWorkspace( buildContext_t *context, BuilderOptions *options ) {
#ifdef _WIN32
	assert( context );
	assert( context->inputFile );
	assert( context->inputFilePath.data );
	assert( options );
	
	const std::vector<BuildConfig>& buildConfigs  = options->configs;

	const TenxWorkspaceOptions& workspace 		  = options->tenxOptions;

	const std::string& name						  = workspace.name;
	const std::string& outputPath 				  = workspace.outputPath;
	const std::string& buildScriptOverride		  = workspace.buildScriptOverride;
	const std::string& debuggerPath 			  = workspace.debuggerPath;
	const std::string& debuggerArgs 			  = workspace.debuggerArgs;
	const std::string& includeFilter 			  = workspace.includeFilter.empty() ? DefaultIncludeFilter : workspace.includeFilter;
	const std::string& excludeFilter 			  = workspace.excludeFilter.empty() ? DefaultExcludeFilter : workspace.excludeFilter;

	const std::vector<TenxPlatform>& platforms 	  = workspace.platforms;
	const std::vector<std::string>& globalDefines = workspace.globalDefines;

	const bool8 includeFilesWithoutExt 			  = workspace.includeFilesWithoutExt;
	const bool8 syncFiles 						  = workspace.syncFiles;
	const bool8 recursive 						  = workspace.recursive;
	const bool8 showEmptyFolders 				  = workspace.showEmptyFolders;
	const bool8 useVisualStudioEnvBat 			  = workspace.useVisualStudioEnvBat;
	const bool8 captureExeOutput 				  = workspace.captureExeOutput;

	const char* inputFilePath 					  = path_absolute_path(path_canonicalise( context->inputFilePath.data ) );
	const char* inputFile 						  = buildScriptOverride.empty() ? path_canonicalise( context->inputFile ) : tprintf( "%s%c%s", inputFilePath, PATH_SEPARATOR, buildScriptOverride.c_str() );

	const char* builderExeFilename				  = tprintf( "%s%c%s", path_remove_file_from_path( path_app_path() ), PATH_SEPARATOR, BUILDER_PROGRAM_NAME );
	const char* builderExePath 					  = path_remove_file_from_path( builderExeFilename );

	const char* workspaceName 					  = name.empty() ? "Workspace" : name.c_str();
	const char* workspacePath 					  = outputPath.empty() ? inputFilePath : tprintf( "%s%c%s", inputFilePath, PATH_SEPARATOR, outputPath.c_str() );
	const char* workspaceFilename 				  = tprintf( "%s%c%s.10x", workspacePath, PATH_SEPARATOR, workspaceName );

	StringBuilder workspaceContent = {};
	string_builder_reset( &workspaceContent );
	defer( string_builder_destroy( &workspaceContent ) );

	string_builder_appendf( &workspaceContent, "<?xml version=\"1.0\"?>\n"	);
	string_builder_appendf( &workspaceContent, "<N10X>\n"					);
	string_builder_appendf( &workspaceContent, "\t<Workspace>\n"			);

	// ===============================================================================================================
	// Filters
	// ===============================================================================================================

	string_builder_appendf(	&workspaceContent, "\t\t<IncludeFilter>%s</IncludeFilter>\n", includeFilter.c_str() );
	string_builder_appendf(	&workspaceContent, "\t\t<ExcludeFilter>%s</ExcludeFilter>\n", excludeFilter.c_str() );

	string_builder_appendf( &workspaceContent, "\t\t<IsFolder>false</IsFolder>\n" ); // we don't want 10x to open our workspace as a folder under any circumstance
	string_builder_appendf( &workspaceContent, "\t\t<IncludeFilesWithoutExt>%s</IncludeFilesWithoutExt>\n", BoolToString(includeFilesWithoutExt).data()		);
	string_builder_appendf( &workspaceContent, "\t\t<SyncFiles>%s</SyncFiles>\n",							BoolToString(syncFiles).data()					);
	string_builder_appendf( &workspaceContent, "\t\t<Recursive>%s</Recursive>\n",							BoolToString(recursive).data()					);
	string_builder_appendf( &workspaceContent, "\t\t<ShowEmptyFolders>%s</ShowEmptyFolders>\n",				BoolToString(showEmptyFolders).data()			);
	string_builder_appendf( &workspaceContent, "\t\t<UseVisualStudioEnvBat>%s</UseVisualStudioEnvBat>\n",	BoolToString(useVisualStudioEnvBat).data()		);
	string_builder_appendf( &workspaceContent, "\t\t<CaptureExeOutput>%s</CaptureExeOutput>\n",				BoolToString(captureExeOutput).data()			);

	// ===============================================================================================================
	// Configs
	// ===============================================================================================================

	string_builder_appendf( &workspaceContent, "\t\t<Configurations>\n" );
	For ( u64, configIndex, 0, buildConfigs.size() ) {
		const BuildConfig& config = buildConfigs[configIndex];
		string_builder_appendf( &workspaceContent, "\t\t\t<Configuration>"	);
		string_builder_appendf( &workspaceContent, config.name.c_str()		);
		string_builder_appendf( &workspaceContent, "</Configuration>\n"		);
	}
	string_builder_appendf( &workspaceContent, "\t\t</Configurations>\n" );

	// ===============================================================================================================
	// Platforms
	// ===============================================================================================================

	string_builder_appendf( &workspaceContent, "\t\t<Platforms>\n" );
	// @NOTE-Ed - Default to x64 if not platforms provided
	if ( platforms.size() == 0 ) {
		string_builder_appendf( &workspaceContent, "\t\t\t<Platform>x64</Platform>\n" );
	} else {
		For (u64, platformIndex, 0, platforms.size()) {
			string_builder_appendf( &workspaceContent, "\t\t\t<Platform>" 					 );
			string_builder_appendf( &workspaceContent, platforms[platformIndex].name.c_str() );
			string_builder_appendf( &workspaceContent, "</Platform>\n" 						 );
		}
	}
	string_builder_appendf( &workspaceContent, "\t\t</Platforms>\n" );

	// ===============================================================================================================
	// Additional Includes
	// ===============================================================================================================

	string_builder_appendf( &workspaceContent, "\t\t<AdditionalIncludePaths>\n" );

	const char* additionalIncludeBaseFmt = "\t\t\t<AdditionalIncludePath>%s</AdditionalIncludePath>\n";
	
	// MSVC Install
	msvcInstall_t msvcInstall;
	Win_GetMSVCInstall( &msvcInstall );

	if ( msvcInstall.includePath.data ) {
		string_builder_appendf( &workspaceContent, additionalIncludeBaseFmt, msvcInstall.includePath.data );
	}

	// Windows SDK include paths (if any)
	windowsSDK_t sdk;
	Win_GetWindowsSDK( &sdk );

	if ( sdk.ucrtInclude.data ) {
		string_builder_appendf( &workspaceContent, additionalIncludeBaseFmt, sdk.ucrtInclude.data );
	}

	if ( sdk.umInclude.data ) {
		string_builder_appendf( &workspaceContent, additionalIncludeBaseFmt, sdk.umInclude.data );
	}

	if ( sdk.sharedInclude.data ) {
		string_builder_appendf( &workspaceContent, additionalIncludeBaseFmt, sdk.sharedInclude.data );
	}

	const char* builderIncludePath = tprintf( "%s%c..%cinclude", builderExePath, PATH_SEPARATOR, PATH_SEPARATOR );
	string_builder_appendf( &workspaceContent, additionalIncludeBaseFmt, builderIncludePath );
	
	const char* builderClangIncludePath = tprintf( "%s%c..%c%s", builderExePath, PATH_SEPARATOR, PATH_SEPARATOR, path_canonicalise("clang/include") );
	string_builder_appendf( &workspaceContent, additionalIncludeBaseFmt, builderClangIncludePath );

	const char* builderClangLibIncludePath = tprintf( "%s%c..%c%s", builderExePath, PATH_SEPARATOR, PATH_SEPARATOR, path_canonicalise("clang/lib/clang/20/include" ) );
	string_builder_appendf( &workspaceContent, additionalIncludeBaseFmt, builderClangLibIncludePath );

	std::unordered_set<std::string> uniqueConfigIncludes;
	for ( const BuildConfig& config : buildConfigs ) {
		const std::vector<std::string>& defines = config.additionalIncludes;
		uniqueConfigIncludes.insert( defines.begin(), defines.end() );
	}

	for ( const std::string& entry : uniqueConfigIncludes ) {
		string_builder_appendf( &workspaceContent, additionalIncludeBaseFmt, path_canonicalise( entry.c_str() ) );
	}

	string_builder_appendf( &workspaceContent, "\t\t</AdditionalIncludePaths>\n" );

	// ===============================================================================================================
	// Defines
	// ===============================================================================================================

	string_builder_appendf( &workspaceContent, "\t\t<Defines>\n" );

#ifdef _WIN32
	string_builder_appendf( &workspaceContent, "\t\t\t<Define>_WIN32</Define>\n" );
#elif __linux__
	string_builder_appendf( &workspaceContent, "\t\t\t<Define>__linux__</Define>\n", define.c_str() );
#endif

	const Compiler compiler = GetCompiler( context, options );

	const char* compilerDefine = nullptr;
	switch ( compiler ){
		case Compiler::COMPILER_DEFAULT:
		case Compiler::COMPILER_CLANG:
			compilerDefine = "__clang__";
			break;
		case Compiler::COMPILER_GCC:
			compilerDefine = "__GNUC__";
			break;
		case Compiler::COMPILER_MSVC:
			compilerDefine = "_MSC_VER";
			break;
	}

	string_builder_appendf( &workspaceContent, "\t\t\t<Define>%s</Define>\n", compilerDefine );

	const std::vector<std::string>& userCompilerDefines = GetUserCompilerDefines( workspace, compiler );
	For ( u64, defineIndex, 0, userCompilerDefines.size() ) {
		string_builder_appendf( &workspaceContent, "\t\t\t<Define>%s</Define>\n", userCompilerDefines[defineIndex].c_str() );
	}

	// Appending this define just for conviniece for the user
	string_builder_appendf( &workspaceContent, "\t\t\t<Define>BUILDER_DOING_USER_CONFIG_BUILD</Define>\n" );

	std::unordered_set<std::string> uniqueGlobalDefines;
	uniqueGlobalDefines.insert( globalDefines.begin(), globalDefines.end() );

	for ( const std::string& entry : uniqueGlobalDefines) {
		string_builder_appendf( &workspaceContent, "\t\t\t<Define>%s</Define>\n", entry.c_str() );
	}

	string_builder_appendf( &workspaceContent, "\t\t</Defines>\n" );

	// ===============================================================================================================
	// Config-Platform Properties
	// ===============================================================================================================

	string_builder_appendf( &workspaceContent, "\t\t<ConfigProperties>\n" );

	if ( platforms.size() > 0 ) {

		For ( u64, platformIndex, 0, platforms.size() ) {
			const TenxPlatform& platform = platforms[platformIndex];
			const char* platformName = platform.name.c_str();

			For ( u64, configIndex, 0, buildConfigs.size() ) {
				const BuildConfig& config = buildConfigs[configIndex];

				const char* configName 				= config.name.c_str();
				const char* binaryName 				= config.binaryName.c_str();
				const char* binaryFolderRelative 	= path_canonicalise( config.binaryFolder.c_str() );
#ifdef	_WIN32
				const char* binaryExtension 		= ".exe";
#elif
				const char* binaryExtension 		= "";
#endif
				const char* binaryFolderPath 		= tprintf( "%s%c%s%c", path_canonicalise( inputFilePath ), PATH_SEPARATOR, path_canonicalise( binaryFolderRelative ), PATH_SEPARATOR );
				const char* binaryFilename   		= tprintf( "%s%s%s", binaryFolderPath, binaryName, binaryExtension );

				string_builder_appendf( &workspaceContent, "\t\t\t<ConfigAndPlatform>\n" );
				string_builder_appendf( &workspaceContent, "\t\t\t\t<Name>%s:%s</Name>\n" , 												configName, 		 platformName					 );
				string_builder_appendf( &workspaceContent, "\t\t\t\t<BuildCommand>%s %s --config=%s</BuildCommand>\n" , 					builderExeFilename,  inputFile, 		configName   );
				string_builder_appendf( &workspaceContent, "\t\t\t\t<RebuildCommand>%s %s --force-rebuild --config=%s</RebuildCommand>\n", 	builderExeFilename,  inputFile, 		configName   );
				string_builder_appendf( &workspaceContent, "\t\t\t\t<BuildFileCommand>%s %s --config=%s</BuildFileCommand>\n", 				builderExeFilename,  inputFile, 		configName   );
				string_builder_appendf( &workspaceContent, "\t\t\t\t<CleanCommand>%s --nuke %s</CleanCommand>\n",							builderExeFilename,  binaryFolderRelative			 );
				string_builder_appendf( &workspaceContent, "\t\t\t\t<CancelCommand></CancelCommand>\n" );
				string_builder_appendf( &workspaceContent, "\t\t\t\t<BuildWorkingDirectory>%s</BuildWorkingDirectory>\n", 					inputFilePath									 	 );
				string_builder_appendf( &workspaceContent, "\t\t\t\t<Exe>%s</Exe>\n", 														binaryFilename 				 						 );
				string_builder_appendf( &workspaceContent, "\t\t\t\t<Args></Args>\n" );
				string_builder_appendf( &workspaceContent, "\t\t\t\t<WorkingDirectory>%s</WorkingDirectory>\n", 							inputFilePath				 						 );
				string_builder_appendf( &workspaceContent, "\t\t\t\t<DebugCommand>$(Executable)</DebugCommand>\n" );
				string_builder_appendf( &workspaceContent, "\t\t\t\t<DebuggerExe>%s %s</DebuggerExe>\n", 									debuggerPath.c_str(), debuggerArgs.c_str()  		 );
				string_builder_appendf( &workspaceContent, "\t\t\t\t<DebugSln></DebugSln>\n" );
				string_builder_appendf( &workspaceContent, "\t\t\t\t<ForceIncludes></ForceIncludes>\n" );
				string_builder_appendf( &workspaceContent, "\t\t\t\t<PrioritiseFiles></PrioritiseFiles>\n" );
				string_builder_appendf( &workspaceContent, "\t\t\t</ConfigAndPlatform>\n" );

				string_builder_appendf( &workspaceContent, "\t\t\t<Config>\n" );
				string_builder_appendf( &workspaceContent, "\t\t\t\t<Name>%s</Name>\n", configName );
				const std::vector<std::string>& configDefines = config.defines;
				if (configDefines.size() == 0) {
					string_builder_appendf( &workspaceContent, "\t\t\t\t<Defines></Defines>\n" );
					string_builder_appendf( &workspaceContent, "\t\t\t</Config>\n" );
					continue;
				}
				string_builder_appendf( &workspaceContent, "\t\t\t\t<Defines>\n" );
				For (u64, defineIndex, 0, configDefines.size()) {
					const std::string& define = configDefines[defineIndex];
					string_builder_appendf( &workspaceContent, "\t\t\t\t\t<Define>%s</Define>\n", define.c_str() );
				}
				string_builder_appendf( &workspaceContent, "\t\t\t\t</Defines>\n" );
				string_builder_appendf( &workspaceContent, "\t\t\t</Config>\n" );
			}

			string_builder_appendf( &workspaceContent, "\t\t\t<Platform>\n" );
			string_builder_appendf( &workspaceContent, "\t\t\t\t<Name>%s</Name>\n", platformName );
			const std::vector<std::string>& platformDefines = platform.defines;
			if (platformDefines.size() == 0) {
				string_builder_appendf( &workspaceContent, "\t\t\t\t<Defines></Defines>\n" );
				string_builder_appendf( &workspaceContent, "\t\t\t</Platform>\n" );
				continue;
			}
			string_builder_appendf( &workspaceContent, "\t\t\t\t<Defines>\n" );
			For (u64, defineIndex, 0, platformDefines.size()) {
				const std::string& define = platformDefines[defineIndex];
				string_builder_appendf( &workspaceContent, "\t\t\t\t\t<Define>%s</Define>\n", define.c_str() );
			}
			string_builder_appendf( &workspaceContent, "\t\t\t\t</Defines>\n" );
			string_builder_appendf( &workspaceContent, "\t\t\t</Platform>\n" );
		}
	}
			
	string_builder_appendf( &workspaceContent, "\t\t</ConfigProperties>\n" );

	string_builder_appendf( &workspaceContent, "\t</Workspace>\n" );
	string_builder_appendf( &workspaceContent, "</N10X>" );

	const char *content = string_builder_to_string( &workspaceContent );
	const u64 contentLength = strlen( content );

	bool8 written = file_write_entire( workspaceFilename, content, contentLength );
	if ( !written ) {
		errorCode_t errorCode = get_last_error_code();
		error( "Failed to write \"%s\": " ERROR_CODE_FORMAT ".\n", workspacePath, errorCode );

		return false;
	}

	options->configs.clear();
	return true;
#elif defined(__linux__)
	(void)context;
	(void)options;
	return true;
#endif
}