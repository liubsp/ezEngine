#pragma once

#include <EditorPluginMcp/EditorPluginMcpDLL.h>
#include <Mcp/Tools/McpAppTool.h>

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Strings/String.h>

/// \brief The editor's half of the shared app tools.
///
/// Everything generic - the process id, the executable, the command line, the bound port - is in
/// ezMcpAppTool. What is left here is what only the editor has: documents that can be unsaved, a log
/// file whose name is derived from '-appid', and a separate engine process to point at.
class EZ_EDITORPLUGINMCP_DLL ezMcpEditorAppTool : public ezMcpAppTool
{
  EZ_ADD_DYNAMIC_REFLECTION(ezMcpEditorAppTool, ezMcpAppTool);

public:
  virtual void GetSupportedTools(ezDynamicArray<ezMcpToolDesc>& out_tools) const override;
  virtual void Execute(ezStringView sToolName, const ezVariantDictionary& arguments, ezMcpToolResult& out_result) override;

protected:
  virtual ezStringView GetHostNoun() const override { return "editor"; }
  virtual ezStringView GetRelaunchHint() const override;

  /// Overridden so that the timestamp is this plugin's, not the Mcp library's: the editor tools live
  /// here and change far more often, so this is the binary whose age explains a missing tool.
  virtual ezStringView GetBuildTimestamp() const override;

  virtual void AddHostInfo(ezMcpJsonWriter& ref_writer) override;

  virtual ezResult CanQuit(bool bDiscardChanges) override;
  virtual void AddQuitRefusalInfo(ezMcpJsonWriter& ref_writer) override;
  virtual void AddQuitInfo(ezMcpJsonWriter& ref_writer) override;
  virtual void RequestQuit(bool bDiscardChanges) override;

private:
  // Provider recreation invalidates targeting tokens held by earlier launch records.
  const ezUuid m_QuitInstance = ezUuid::MakeUuid();

  /// \brief Appends the names of all open documents with unsaved changes.
  static void CollectModifiedDocuments(ezDynamicArray<ezString>& out_documents);

  /// Filled by CanQuit() so that the refusal can name them without walking the documents twice, and so
  /// that a quit which goes ahead can report how many changes it discarded.
  ezDynamicArray<ezString> m_ModifiedDocuments;
};
