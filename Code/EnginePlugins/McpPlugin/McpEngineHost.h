#pragma once

#include <McpPlugin/McpPluginDLL.h>

#include <Core/GameApplication/GameApplicationBase.h>

class ezMcpServer;

/// \brief Owns the MCP server inside a game process, and the per-frame bookkeeping its tools need.
///
/// The counterpart to what EditorPluginMcp does in the editor. The difference that shapes this class is
/// that a game process has a frame loop: 'run for a while and then look' is a meaningful request here,
/// so tools need a frame number to wait for, and shutting down has to be deferred until after a
/// response has left the socket.
///
/// Everything is static because there is one process, one server and one frame loop. There is nothing a
/// second instance would mean.
class EZ_MCPPLUGIN_DLL ezMcpEngineHost
{
public:
  /// \brief Resolves the port, starts the server and subscribes to the frame loop.
  ///
  /// Does nothing without control intent (see ezRuntimeControlOptions). Loading this plugin alone
  /// must not cause an application to open a listener.
  static void Startup();

  static void Shutdown();

  /// \brief How many frames the application has *rendered* since the server started.
  ///
  /// Monotonic, and the unit that game_wait counts in. Counts presents, not application ticks: the
  /// editor's engine process loops continuously while rendering nothing, so ticks would report progress
  /// that a screenshot or an input frame never sees.
  static ezUInt64 GetFrameCount() { return s_uiFrameCount; }

  /// \brief Shuts the process down at the end of the current frame.
  ///
  /// Deferred, because the response to app_quit has not reached the socket yet - see
  /// ezMcpAppTool::RequestQuit(). Quitting from inside the tool call would drop it.
  static void RequestQuit();

private:
  static void ExecutionEventHandler(const ezGameApplicationExecutionEvent& e);

  static ezMcpServer* s_pServer;
  static ezUInt64 s_uiFrameCount;
  static bool s_bQuitRequested;
};
