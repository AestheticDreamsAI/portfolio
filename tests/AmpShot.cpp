// Screenshot der Amp-Oberflaeche (Gitarre und Bass)
#include "../Source/Amp/AmpProcessor.h"
#include "../Source/Amp/AmpEditor.h"
int main (int, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    AmpProcessor p; p.setPlayConfigDetails (2, 2, 48000, 512); p.prepareToPlay (48000, 512);
    for (int mode = 0; mode < 1; ++mode)
    {
        
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        juce::File f { juce::String (argv[1]) + (mode ? "_bass.png" : "_gitarre.png") };
        f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
    }
    return 0;
}
