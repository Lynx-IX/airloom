#pragma once

#include <JuceHeader.h>
#include "AppFolder.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <dbghelp.h>
 #pragma comment (lib, "dbghelp.lib")
#endif

// Finding out why Airloom closed when it shouldn't have.
//   - A crash writes Crashes\Airloom <date time>.dmp (a minidump, opened against the build's
//     Airloom.pdb) and a line with the stack to crash-log.txt, in %APPDATA%\Airloom (or LiveRig, see AppFolder).
//   - A "running" marker is left while Airloom is open and removed when it quits properly, so
//     the next start knows if the last one ended any other way (even one no crash handler sees).
namespace CrashLog
{
    inline juce::File folder()
    {
        return AppFolder::get();
    }

    inline juce::File logFile()       { return folder().getChildFile ("crash-log.txt"); }
    inline juce::File dumpFolder()    { return folder().getChildFile ("Crashes"); }
    inline juce::File runningMarker() { return folder().getChildFile ("running.marker"); }

    // Set at startup: how the previous run ended, if not by quitting.
    inline juce::String& previousRunProblem()
    {
        static juce::String problem;
        return problem;
    }

    inline void writeLine (const juce::String& text)
    {
        logFile().appendText (juce::Time::getCurrentTime().toString (true, true, true, true) + "  " + text + "\n", false, false, "\r\n");
    }

    inline void onCrash (void* exceptionInfo)
    {
        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
        juce::String where;

       #if JUCE_WINDOWS
        // A minidump: the state of every thread at the crash, for a debugger.
        dumpFolder().createDirectory();
        const auto dump = dumpFolder().getChildFile ("Airloom " + stamp + ".dmp");
        const HANDLE file = CreateFileW (dump.getFullPathName().toWideCharPointer(), GENERIC_WRITE, 0, nullptr,
                                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION info { GetCurrentThreadId(), static_cast<EXCEPTION_POINTERS*> (exceptionInfo), FALSE };
            MiniDumpWriteDump (GetCurrentProcess(), GetCurrentProcessId(), file,
                               (MINIDUMP_TYPE) (MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo),
                               exceptionInfo != nullptr ? &info : nullptr, nullptr, nullptr);
            CloseHandle (file);
            where = "  dump: " + dump.getFullPathName();
        }

        if (auto* pointers = static_cast<EXCEPTION_POINTERS*> (exceptionInfo); pointers != nullptr && pointers->ExceptionRecord != nullptr)
            where = "  code 0x" + juce::String::toHexString ((int) pointers->ExceptionRecord->ExceptionCode)
                  + " at 0x" + juce::String::toHexString ((juce::pointer_sized_int) pointers->ExceptionRecord->ExceptionAddress) + where;
       #endif

        writeLine ("CRASH" + where + "\n" + juce::SystemStats::getStackBacktrace());
    }

    // Call first thing at startup.
    inline void start()
    {
        folder().createDirectory();
        if (runningMarker().existsAsFile())
        {
            const auto since = runningMarker().loadFileAsString().trim();
            previousRunProblem() = "Airloom closed unexpectedly last time" + (since.isNotEmpty() ? " (started " + since + ")" : juce::String())
                                 + " - details in " + logFile().getFullPathName();
            writeLine ("Previous run (started " + since + ") ended without quitting");
        }
        runningMarker().replaceWithText (juce::Time::getCurrentTime().toString (true, true, true, true));
        juce::SystemStats::setApplicationCrashHandler (onCrash);
    }

    // Call when quitting properly.
    inline void stop()      { runningMarker().deleteFile(); }
}
