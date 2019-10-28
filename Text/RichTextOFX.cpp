/*
####################################################################
#
# Copyright (C) 2019 Ole-André Rodlie <ole.andre.rodlie@gmail.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
####################################################################
*/

#include "RichText.h"
#include "ofxsImageEffect.h"

#include <iostream>
#include <stdlib.h>

#define kPluginName "RichTextOFX"
#define kPluginIdentifier "net.fxarena.openfx.RichText"
#define kPluginVersionMajor 0
#define kPluginVersionMinor 7
#define kPluginGrouping "Draw"
#define kPluginDescription "Rich Text Generator for Natron.\n\nUnder development, also require changes in Natron to work as intended."

#define kSupportsTiles 0
#define kSupportsMultiResolution 0
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

#define kParamHTML "text"
#define kParamHTMLLabel "Text"
#define kParamHTMLHint "The text that will be drawn."

// https://doc.qt.io/qt-5/richtext-html-subset.html
#define kParamHTMLDefault \
"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">" \
"<html>" \
"<head>" \
"<meta name=\"qrichtext\" content=\"1\" />" \
"<style type=\"text/css\">" \
"p, li { white-space: pre-wrap; }" \
"</style>" \
"</head>" \
"<body style=\" font-family:'Droid Sans'; font-size:21pt; font-weight:400; font-style:normal;\">" \
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">OpenFX Rich Text</p>" \
"</body>" \
"</html>" \

#define kParamAlign "align"
#define kParamAlignLabel "Align"
#define kParamAlignHint "Text alignment"

#define kParamWrap "wrap"
#define kParamWrapLabel "Wrap"
#define kParamWrapHint "Word wrap"

#define kParamJustify "justify"
#define kParamJustifyLabel "Justify"
#define kParamJustifyHint "Text justify."
#define kParamJustifyDefault false

static std::string ofxPath;

using namespace OFX;

class RichTextPlugin : public ImageEffect
{
public:
    RichTextPlugin(OfxImageEffectHandle handle);
    virtual void render(const RenderArguments &args) override final;
    virtual void changedParam(const InstanceChangedArgs &args,
                              const std::string &paramName) override final;

private:
    Clip *_dstClip;
    StringParam *_srcText;
    ChoiceParam *_srcAlign;
    ChoiceParam *_srcWrap;
    BooleanParam *_srcJustify;
    FcConfig *_fc;
};

RichTextPlugin::RichTextPlugin(OfxImageEffectHandle handle)
: ImageEffect(handle)
, _dstClip(nullptr)
, _srcText(nullptr)
, _srcAlign(nullptr)
, _srcWrap(nullptr)
, _srcJustify(nullptr)
{
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);
    assert(_dstClip && _dstClip->getPixelComponents() == ePixelComponentRGBA);

    _srcText = fetchStringParam(kParamHTML);
    _srcAlign = fetchChoiceParam(kParamAlign);
    _srcWrap = fetchChoiceParam(kParamWrap);
    _srcJustify = fetchBooleanParam(kParamJustify);
    assert(_srcText && _srcAlign && _srcWrap && _srcJustify);

    // setup fontconfig
    std::string fontConf = ofxPath;
    fontConf.append("/Contents/Resources/fonts");
    if (RichText::fileExists(fontConf+"/fonts.conf")) {
#ifdef _WIN32
        _putenv_s("FONTCONFIG_PATH", fontConf.c_str());
#else
        setenv("FONTCONFIG_PATH", fontConf.c_str(), 1);
#endif
    }
    _fc = FcInitLoadConfigAndFonts();
}

/* Override the render */
void RichTextPlugin::render(const RenderArguments &args)
{
    // renderscale
    if (!kSupportsRenderScale &&
        (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        setPersistentMessage(Message::eMessageError, "", "Renderscale (render)");
        throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    // dstclip
    if (!_dstClip) {
        setPersistentMessage(Message::eMessageError, "", "No destination clip!");
        throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    assert(_dstClip);

    // get dstclip
    auto_ptr<Image> dstImg(_dstClip->fetchImage(args.time));
    if (!dstImg.get()) {
        setPersistentMessage(Message::eMessageError, "", "No destination image!");
        throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    // renderscale
    checkBadRenderScaleOrField(dstImg, args);

    // get bitdepth
    BitDepthEnum dstBitDepth = dstImg->getPixelDepth();
    if (dstBitDepth != eBitDepthFloat) {
        setPersistentMessage(Message::eMessageError, "", "Image depth is not float!");
        throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    // get channels
    PixelComponentEnum dstComponents  = dstImg->getPixelComponents();
    if (dstComponents != ePixelComponentRGBA) {
        setPersistentMessage(Message::eMessageError, "", "Image is not RGBA!");
        throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    // are we in the image bounds
    OfxRectI dstBounds = dstImg->getBounds();
    OfxRectI dstRod = dstImg->getRegionOfDefinition();
    if (args.renderWindow.x1 < dstBounds.x1 ||
        args.renderWindow.x1 >= dstBounds.x2 ||
        args.renderWindow.y1 < dstBounds.y1 ||
        args.renderWindow.y1 >= dstBounds.y2 ||
        args.renderWindow.x2 <= dstBounds.x1 ||
        args.renderWindow.x2 > dstBounds.x2 ||
        args.renderWindow.y2 <= dstBounds.y1 ||
        args.renderWindow.y2 > dstBounds.y2) {
        setPersistentMessage(Message::eMessageError, "", "Image bounds failed!");
        throwSuiteStatusException(kOfxStatErrValue);
        return;
    }

    // get options
    int width = dstRod.x2-dstRod.x1;
    int height = dstRod.y2-dstRod.y1;
    std::string html;
    int align = RichText::RichTextAlignLeft;
    int wrap = RichText::RichTextWrapWord;
    bool justify = kParamJustifyDefault;

    _srcText->getValueAtTime(args.time, html);
    _srcAlign->getValueAtTime(args.time, align);
    _srcWrap->getValueAtTime(args.time, wrap);
    _srcJustify->getValueAtTime(args.time, justify);

    // render
    RichText::RichTextRenderResult result = RichText::renderRichText(width,
                                                                     height,
                                                                     _fc,
                                                                     html,
                                                                     wrap,
                                                                     align,
                                                                     justify,
                                                                     args.renderScale.x,
                                                                     args.renderScale.y,
                                                                     true /* flip */);
    if (!result.success) {
        setPersistentMessage(Message::eMessageError, "", "RichText Renderer failed");
        throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    // write output
    float* pixelData = (float*)dstImg->getPixelData();
    int offset = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            pixelData[offset + 0] = result.buffer[offset + 0] * (1.f / 255);
            pixelData[offset + 1] = result.buffer[offset + 1] * (1.f / 255);
            pixelData[offset + 2] = result.buffer[offset + 2] * (1.f / 255);
            pixelData[offset + 3] = result.buffer[offset + 3] * (1.f / 255);
            offset += 4;
        }
    }
    delete [] result.buffer;
    result.buffer = nullptr;
    pixelData = nullptr;
}

void RichTextPlugin::changedParam(const InstanceChangedArgs &args,
                                  const std::string &paramName)
{
    clearPersistentMessage();
    if (!kSupportsRenderScale &&
        (args.renderScale.x != 1. || args.renderScale.y != 1.))
    {
        setPersistentMessage(Message::eMessageError, "", "Renderscale (changed param)");
        throwSuiteStatusException(kOfxStatFailed);
        return;
    }
}

mDeclarePluginFactory(RichTextPluginFactory, {}, {});

/** @brief The basic describe function, passed a plugin descriptor */
void RichTextPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts
    desc.addSupportedContext(eContextGenerator);

    // add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthFloat);

    // add other
    desc.setSupportsTiles(kSupportsTiles);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setRenderThreadSafety(kRenderThreadSafety);
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void RichTextPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                              ContextEnum /*context*/)
{
    // set path
    std::string path;
    ofxPath = desc.getPropertySet().propGetString(kOfxPluginPropFilePath, false);

    // there has to be an input clip, even for generators
    ClipDescriptor* srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->setOptional(true);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages
    PageParamDescriptor *page = desc.definePageParam("Controls");
    {
        StringParamDescriptor* param = desc.defineStringParam(kParamHTML);
        param->setLabel(kParamHTMLLabel);
        param->setHint(kParamHTMLHint);
        param->setStringType(eStringTypeRichTextFormat);
        param->setAnimates(true); // Don't know if this works...
        param->setDefault(kParamHTMLDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamAlign);
        param->setLabel(kParamAlignLabel);
        param->setHint(kParamAlignHint);
        param->appendOption("Left"); // RichTextAlignLeft
        param->appendOption("Right"); // RichTextAlignRight
        param->appendOption("Center"); // RichTextAlignCenter
        param->setDefault(RichText::RichTextAlignLeft);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamWrap);
        param->setLabel(kParamWrapLabel);
        param->setHint(kParamWrapHint);
        param->appendOption("Word"); // RichTextWrapWord
        param->appendOption("Char"); // RichTextWrapChar
        param->appendOption("Word-Char"); // RichTextWrapWordChar
        param->setDefault(RichText::RichTextWrapWord);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamJustify);
        param->setLabel(kParamJustifyLabel);
        param->setHint(kParamJustifyHint);
        param->setDefault(kParamJustifyDefault);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* RichTextPluginFactory::createInstance(OfxImageEffectHandle handle,
                                                   ContextEnum /*context*/)
{
    return new RichTextPlugin(handle);
}

static RichTextPluginFactory p(kPluginIdentifier,
                               kPluginVersionMajor,
                               kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)
