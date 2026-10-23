#pragma once

#include <Mcp/McpTool.h>

/// Project discovery, runtime profile saving and export for the editor MCP host.
class ezMcpProjectTool : public ezMcpToolProvider
{
  EZ_ADD_DYNAMIC_REFLECTION(ezMcpProjectTool, ezMcpToolProvider);

public:
  virtual void GetSupportedTools(ezDynamicArray<ezMcpToolDesc>& out_tools) const override;
  virtual void Execute(ezStringView sToolName, const ezVariantDictionary& arguments, ezMcpToolResult& out_result) override;

private:
  void ExecuteProjectInfo(const ezVariantDictionary& arguments, ezMcpToolResult& out_result);
  void ExecuteProjectExport(const ezVariantDictionary& arguments, ezMcpToolResult& out_result);
  void ExecuteSaveRuntimeProfile(const ezVariantDictionary& arguments, ezMcpToolResult& out_result);
};
