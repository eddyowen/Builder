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

#include "core/include/core_types.h"
#include "core/include/debug.h"
#include "core/include/string_builder.h"
#include "core/include/string_helpers.h"
#include "core/include/file.h"
#include "core/include/paths.h"

bool8 Generate10xWorkspace( buildContext_t *context, BuilderOptions *options ) {
	assert( context );
	assert( context->inputFile );
	assert( context->inputFilePath.data );
	assert( options );
	
	const char *workspacePath = NULL;

	const char* inputFilePath = context->inputFilePath.data;

	const EditorProjectDefinition& projectDefinition = options->projectDefinition;

	const std::string& outputPath = projectDefinition.outputPath;
	if ( !outputPath.c_str() ) {
		workspacePath = tprintf( "%s%c%s%stest.10x", inputFilePath, PATH_SEPARATOR, outputPath.c_str(), PATH_SEPARATOR );
	} else {
		workspacePath = tprintf( "%s%cstest.10x", inputFilePath, PATH_SEPARATOR );
	}

	auto WriteStringBuilderToFile = []( StringBuilder *stringBuilder, const char *filename ) -> bool8 {
		const char *msg = string_builder_to_string( stringBuilder );
		const u64 msgLength = strlen( msg );
		bool8 written = file_write_entire( filename, msg, msgLength );

		if ( !written ) {
			errorCode_t errorCode = get_last_error_code();
			error( "Failed to write \"%s\": " ERROR_CODE_FORMAT ".\n", filename, errorCode );

			return false;
		}

		return true;
	};

	StringBuilder workspaceContent = {};
	string_builder_reset( &workspaceContent );
	defer( string_builder_destroy( &workspaceContent ) );

	string_builder_appendf( &workspaceContent, "<?xml version=\"1.0\"?>\n" );
	string_builder_appendf( &workspaceContent, "<N10X>\n" );
	string_builder_appendf( &workspaceContent, "\t<Workspace>\n" );

	for ( const auto& [ settingName, valuesList ] : projectDefinition.additionalSettings ) {
		
		if ( settingName.empty() ) {
			continue;
		}

		if (valuesList.size() == 1) {
			const char* value = valuesList[0].c_str();
			assert( value );

			const char* name = settingName.c_str();
			string_builder_appendf( &workspaceContent, "\t\t<%s>%s</%s>\n", name, value, name );
		}
	}

	string_builder_appendf( &workspaceContent, "\t\t<Configurations>\n" );
	For (u64, configIndex, 0, options->configs.size()) {
		BuildConfig& config = options->configs[configIndex];
		string_builder_appendf( &workspaceContent, "\t\t\t<Configuration>" );
		string_builder_appendf( &workspaceContent, config.name.c_str() );
		string_builder_appendf( &workspaceContent, "</Configuration>\n" );
	}
	string_builder_appendf( &workspaceContent, "\t\t</Configurations>\n" );

	const auto platformsIt = projectDefinition.additionalSettings.find("Platforms");
	// @NOTE-Ed - Default to x64 if not platforms provided
	string_builder_appendf( &workspaceContent, "\t\t<Platforms>\n" );
	if (platformsIt == projectDefinition.additionalSettings.end())
	{
		string_builder_appendf( &workspaceContent, "\t\t\t<Platform>x64</Platform>" );
	}
	else
	{
		const std::vector<std::string>& values = platformsIt->second;
		For (u64, platformIndex, 0, values.size()) {
			string_builder_appendf( &workspaceContent, "\t\t\t<Platform>" );
			string_builder_appendf( &workspaceContent, values[platformIndex].c_str() );
			string_builder_appendf( &workspaceContent, "</Platform>\n" );
		}
	}
	string_builder_appendf( &workspaceContent, "\t\t</Platforms>\n" );

	string_builder_appendf( &workspaceContent, "\t</Workspace>\n" );
	string_builder_appendf( &workspaceContent, "</N10X>" );

	options->configs.clear();

	if ( !WriteStringBuilderToFile( &workspaceContent, workspacePath ) ) {
		return false;
	}

	return true;
}
