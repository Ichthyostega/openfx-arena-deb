/*
# Copyright (c) 2015, FxArena DA <mail@fxarena.net>
# All rights reserved.
#
# OpenFX-Arena is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License version 2. You should have received a copy of the GNU General Public License version 2 along with OpenFX-Arena. If not, see http://www.gnu.org/licenses/.
# OpenFX-Arena is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
# Need custom licensing terms or conditions? Commercial license for proprietary software? Contact us.
*/

#include "Wave.h"
#include "ofxsMacros.h"
#include "ofxsMultiThread.h"
#include <Magick++.h>
#include <iostream>
#include <stdint.h>
#include <cmath>

#define kPluginName "WaveOFX"
#define kPluginGrouping "Transform"
#define kPluginIdentifier "net.fxarena.openfx.Wave"
#define kPluginVersionMajor 2
#define kPluginVersionMinor 0
#define kPluginMagickVersion 26640

#define kParamWaveAmp "amp"
#define kParamWaveAmpLabel "Amplitude"
#define kParamWaveAmpHint "Adjust wave amplitude"
#define kParamWaveAmpDefault 25

#define kParamWaveLength "length"
#define kParamWaveLengthLabel "Length"
#define kParamWaveLengthHint "Adjust wave length"
#define kParamWaveLengthDefault 150

#define kSupportsTiles 0
#define kSupportsMultiResolution 0
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe
#define kHostFrameThreading false

using namespace OFX;

class WavePlugin : public OFX::ImageEffect
{
public:
    WavePlugin(OfxImageEffectHandle handle);
    virtual ~WavePlugin();
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
private:
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *maskClip_;
    OFX::DoubleParam *waveAmp_;
    OFX::DoubleParam *waveLength_;
};

WavePlugin::WavePlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
, dstClip_(0)
, srcClip_(0)
{
    Magick::InitializeMagick(NULL);
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    assert(dstClip_ && dstClip_->getPixelComponents() == OFX::ePixelComponentRGBA);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert(srcClip_ && srcClip_->getPixelComponents() == OFX::ePixelComponentRGBA);
    maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
    assert(!maskClip_ || maskClip_->getPixelComponents() == OFX::ePixelComponentAlpha);

    waveAmp_ = fetchDoubleParam(kParamWaveAmp);
    waveLength_ = fetchDoubleParam(kParamWaveLength);

    assert(waveAmp_ && waveLength_);
}

WavePlugin::~WavePlugin()
{
}

void WavePlugin::render(const OFX::RenderArguments &args)
{
    // render scale
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    // get src clip
    if (!srcClip_) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    assert(srcClip_);
    std::auto_ptr<const OFX::Image> srcImg(srcClip_->fetchImage(args.time));
    OfxRectI srcRod,srcBounds;
    if (srcImg.get()) {
        srcRod = srcImg->getRegionOfDefinition();
        srcBounds = srcImg->getBounds();
        if (srcImg->getRenderScale().x != args.renderScale.x ||
            srcImg->getRenderScale().y != args.renderScale.y ||
            srcImg->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }
    }

    // Get mask clip
    std::auto_ptr<const OFX::Image> maskImg((getContext() != OFX::eContextFilter && maskClip_ && maskClip_->isConnected()) ? maskClip_->fetchImage(args.time) : 0);
    OfxRectI maskRod;
    if (getContext() != OFX::eContextFilter && maskClip_ && maskClip_->isConnected())
        maskRod=maskImg->getRegionOfDefinition();

    // get dest clip
    if (!dstClip_) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    assert(dstClip_);
    std::auto_ptr<OFX::Image> dstImg(dstClip_->fetchImage(args.time));
    if (!dstImg.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    if (dstImg->getRenderScale().x != args.renderScale.x ||
        dstImg->getRenderScale().y != args.renderScale.y ||
        dstImg->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    // get bit depth
    OFX::BitDepthEnum dstBitDepth = dstImg->getPixelDepth();
    if (dstBitDepth != OFX::eBitDepthFloat || (srcImg.get() && (dstBitDepth != srcImg->getPixelDepth()))) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    // get pixel component
    OFX::PixelComponentEnum dstComponents  = dstImg->getPixelComponents();
    if (dstComponents != OFX::ePixelComponentRGBA || (srcImg.get() && (dstComponents != srcImg->getPixelComponents()))) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    // are we in the image bounds?
    OfxRectI dstBounds = dstImg->getBounds();
    if(args.renderWindow.x1 < dstBounds.x1 || args.renderWindow.x1 >= dstBounds.x2 || args.renderWindow.y1 < dstBounds.y1 || args.renderWindow.y1 >= dstBounds.y2 ||
       args.renderWindow.x2 <= dstBounds.x1 || args.renderWindow.x2 > dstBounds.x2 || args.renderWindow.y2 <= dstBounds.y1 || args.renderWindow.y2 > dstBounds.y2) {
        OFX::throwSuiteStatusException(kOfxStatErrValue);
        return;
    }

    // get params
    double waveAmp, waveLength;
    waveAmp_->getValueAtTime(args.time, waveAmp);
    waveLength_->getValueAtTime(args.time, waveLength);

    // setup
    int width = srcRod.x2-srcRod.x1;
    int height = srcRod.y2-srcRod.y1;

    // Set max threads allowed by host
    unsigned int threads = 0;
    threads = OFX::MultiThread::getNumCPUs();
    if (threads>0) {
        Magick::ResourceLimits::thread(threads);
        #ifdef DEBUG
        std::cout << "Setting max threads to " << threads << std::endl;
        #endif
    }

    // read image
    Magick::Image image(Magick::Geometry(width,height),Magick::Color("rgba(0,0,0,0)"));
    if (srcClip_ && srcClip_->isConnected())
        image.read(width,height,"RGBA",Magick::FloatPixel,(float*)srcImg->getPixelData());

    #ifdef DEBUG
    image.debug(true);
    #endif

    // apply mask
    if (maskClip_ && maskClip_->isConnected()) {
        int maskWidth = maskRod.x2-maskRod.x1;
        int maskHeight = maskRod.y2-maskRod.y1;
        if (maskWidth>0 && maskHeight>0) {
            Magick::Image mask(maskWidth,maskHeight,"A",Magick::FloatPixel,(float*)maskImg->getPixelData());
            image.composite(mask,0,0,Magick::CopyOpacityCompositeOp);
            Magick::Image container(Magick::Geometry(width,height),Magick::Color("rgba(0,0,0,0)"));
            container.composite(image,0,0,Magick::OverCompositeOp);
            image=container;
        }
    }

    // wave
    image.backgroundColor(Magick::Color("rgba(0,0,0,0)"));
    image.wave(std::floor(waveAmp * args.renderScale.x + 0.5),std::floor(waveLength * args.renderScale.x + 0.5));

    // return image
    if (dstClip_ && dstClip_->isConnected() && srcClip_ && srcClip_->isConnected())
        image.write(0,0,width,height,"RGBA",Magick::FloatPixel,(float*)dstImg->getPixelData());
}

bool WavePlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return false;
    }
    if (srcClip_ && srcClip_->isConnected()) {
        rod = srcClip_->getRegionOfDefinition(args.time);
    } else {
        rod.x1 = rod.y1 = kOfxFlagInfiniteMin;
        rod.x2 = rod.y2 = kOfxFlagInfiniteMax;
    }
    return true;
}

mDeclarePluginFactory(WavePluginFactory, {}, {});

/** @brief The basic describe function, passed a plugin descriptor */
void WavePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    size_t magickNumber;
    std::string magickString = MagickCore::GetMagickVersion(&magickNumber);
    if (magickNumber != kPluginMagickVersion)
        magickString.append("\n\nWarning! You are using an unsupported version of ImageMagick.");
    desc.setPluginDescription("Wave transform node.\n\nPowered by "+magickString);

    // add the supported contexts
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextFilter);

    // add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthFloat);

    desc.setSupportsTiles(kSupportsTiles);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    desc.setHostFrameThreading(kHostFrameThreading);
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void WavePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum /*context*/)
{
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create optional mask clip
    ClipDescriptor *maskClip = desc.defineClip("Mask");
    maskClip->addSupportedComponent(OFX::ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    maskClip->setOptional(true);
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages
    PageParamDescriptor *page = desc.definePageParam(kPluginName);
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamWaveAmp);
        param->setLabel(kParamWaveAmpLabel);
        param->setHint(kParamWaveAmpHint);
        param->setRange(0, 1000);
        param->setDisplayRange(0, 500);
        param->setDefault(kParamWaveAmpDefault);
        page->addChild(*param);
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamWaveLength);
        param->setLabel(kParamWaveLengthLabel);
        param->setHint(kParamWaveLengthHint);
        param->setRange(0, 1000);
        param->setDisplayRange(0, 500);
        param->setDefault(kParamWaveLengthDefault);
        page->addChild(*param);
    }
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* WavePluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new WavePlugin(handle);
}

void getWavePluginID(OFX::PluginFactoryArray &ids)
{
    static WavePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
