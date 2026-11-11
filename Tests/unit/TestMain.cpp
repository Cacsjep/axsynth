#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Runs every juce::UnitTest registered in this executable.
// Usage: AxSynthTests [category]   (exit code 0 = all passed)
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    if (argc > 1)
        runner.runTestsInCategory (argv[1]);
    else
        runner.runAllTests();

    int failures = 0, tests = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* r = runner.getResult (i);
        failures += r->failures;
        tests += r->passes + r->failures;

        if (r->failures > 0)
        {
            std::printf ("FAILED: %s / %s (%d)\n", r->unitTestName.toRawUTF8(), r->subcategoryName.toRawUTF8(), r->failures);
            for (const auto& m : r->messages)
                std::printf ("    %s\n", m.toRawUTF8());
        }
    }

    std::printf ("\n%d checks, %d failures\n", tests, failures);
    return failures == 0 && tests > 0 ? 0 : 1;
}
