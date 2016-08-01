#include "CameraLocalizerPluginFactory.hpp"
#include "CameraLocalizerPluginDefinition.hpp"
#include "CameraLocalizerPlugin.hpp"
#include "CameraLocalizerInteract.hpp"

#include <array>

/**
 * OFX::Plugin::getPluginIDs
 * @param ids
 */
void OFX::Plugin::getPluginIDs(OFX::PluginFactoryArray &ids)
{
  static openMVG_ofx::Localizer::CameraLocalizerPluginFactory p("openmvg.cameralocalizer", 1, 0);
  ids.push_back(&p);
}


namespace openMVG_ofx {
namespace Localizer { 

void CameraLocalizerPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
{
  //Plugin Labels
  desc.setLabels(
    "CameraLocalizer",
    "CameraLocalizer",
    "openMVG CameraLocalizer");

  //Plugin grouping
  desc.setPluginGrouping("openMVG");

  //Plugin description
  desc.setPluginDescription(
    "CameraLocalizer estimates the camera pose of an image "
    "regarding an existing 3D reconstruction generated by openMVG."
    "\n"
    "The plugin supports multiple clips in input to localize a RIG of cameras "
    "(multiple cameras rigidly fixed)."
    );

  //Supported contexts
  desc.addSupportedContext(OFX::eContextFilter);
  desc.addSupportedContext(OFX::eContextGeneral);
  desc.addSupportedContext(OFX::eContextPaint);

  //Supported pixel depths
  desc.addSupportedBitDepth(OFX::eBitDepthUByte);
  desc.addSupportedBitDepth(OFX::eBitDepthUShort);
  desc.addSupportedBitDepth(OFX::eBitDepthFloat);

  //Flags
  desc.setSingleInstance(false);
  desc.setHostFrameThreading(false);
  desc.setSupportsMultiResolution(true);
  desc.setSupportsTiles(false);
  desc.setTemporalClipAccess(false);
  desc.setRenderTwiceAlways(false);
  desc.setSupportsMultipleClipPARs(false);

  desc.setOverlayInteractDescriptor( new CameraLocalizerOverlayDescriptor);
}

void CameraLocalizerPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context)
{
  //Input Clips
  for(unsigned int input = 0; input < K_MAX_INPUTS; ++input)
  {
    OFX::ClipDescriptor *srcClip = desc.defineClip(kClip(input));
    srcClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(false);
    srcClip->setIsMask(false);
    srcClip->setOptional(input > 0);
  }

  //Output clip
  OFX::ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
  dstClip->addSupportedComponent(OFX::ePixelComponentRGBA);
  dstClip->setSupportsTiles(false);
  
  //UI Parameters
  {
    OFX::GroupParamDescriptor *groupMain = desc.defineGroupParam(kParamGroupMain);
    groupMain->setLabel("Settings");
    groupMain->setAsTab();

    {
      OFX::IntParamDescriptor *param = desc.defineIntParam(kParamOutputIndex);
      param->setLabel("Camera Output Index");
      param->setHint("The index of the input clip to expose as the output camera.");
      param->setRange(0, K_MAX_INPUTS);
      // DisplayRange will be updated regarding the number of connected input clips
      param->setDisplayRange(0, 0);
      param->setAnimates(false);
      param->setEnabled(false);
      param->setParent(*groupMain);
      param->setLayoutHint(OFX::eLayoutHintDivider);
    }

    {
      OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamFeaturesType);
      param->setLabel("Features Type");
      param->setHint("Type of descriptors to use");
      param->appendOptions(kStringParamFeaturesType);
      param->setDefault(eParamFeaturesTypeSIFT);
          param->setParent(*groupMain);
    }

    {
      OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamFeaturesPreset);
      param->setLabel("Features Preset");
      param->setHint("Preset for the feature extractor when localizing a new image");
      param->appendOptions(kStringParamFeaturesPreset);
      param->setDefault(eParamFeaturesPresetNormal);
      param->setParent(*groupMain);
    }

    {
      OFX::StringParamDescriptor *param = desc.defineStringParam(kParamReconstructionFile);
      param->setLabel("Reconstruction File");
      param->setHint("3D reconstruction file performed with openMVG (*.abc, *.json, *.bin)");
      param->setStringType(OFX::eStringTypeFilePath);
      param->setFilePathExists(true);
      param->setParent(*groupMain);
    }

    {
      OFX::StringParamDescriptor *param = desc.defineStringParam(kParamDescriptorsFolder);
      param->setLabel("Descriptors Folder");
      param->setHint("3D reconstruction descriptors folder"); 
      param->setStringType(OFX::eStringTypeDirectoryPath);
      param->setFilePathExists(true);
      param->setParent(*groupMain);
    }

    {
      OFX::StringParamDescriptor *param = desc.defineStringParam(kParamVoctreeFile);
      param->setLabel("Voctree File");
      param->setHint("Vocabulary tree is a precomputed file required to match image descriptors (*.tree)");
      param->setStringType(OFX::eStringTypeFilePath);
      param->setFilePathExists(true);
      char* voctreeFilepath = std::getenv("DEFAULT_GENERIC_VOCTREE");
      if(!voctreeFilepath)
        voctreeFilepath = std::getenv("OPENMVG_VOCTREE");

      if(voctreeFilepath)
        param->setDefault(voctreeFilepath);
      param->setParent(*groupMain);
    }

    {
      OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamRigMode);
      param->setLabel("Rig Status");
      param->setHint("Is the camera rig calibrated?");
      param->appendOptions(kStringParamRigMode);
      param->setDefault(eParamRigModeUnKnown);
      param->setParent(*groupMain);
    }

    {
      OFX::StringParamDescriptor *param = desc.defineStringParam(kParamRigCalibrationFile);
      param->setLabel("Rig Calibration File");
      param->setHint("Rig calibration file"); //TODO FACA
      param->setStringType(OFX::eStringTypeFilePath);
      param->setFilePathExists(true);
      param->setParent(*groupMain);
      param->setLayoutHint(OFX::eLayoutHintDivider);
    }

    //Inputs Tabs
    for(unsigned int input = 0; input < K_MAX_INPUTS; ++input)
    {
      OFX::GroupParamDescriptor *groupInput = desc.defineGroupParam(kParamGroupInput(input));
      groupInput->setLabel("Input " + kClip(input));
      groupInput->setAsTab();
      groupInput->setParent(*groupMain);

      //Input is Grayscale
      {
        OFX::BooleanParamDescriptor *param = desc.defineBooleanParam(kParamInputIsGrayscale(input));
        param->setLabel("Is Grayscale");
        param->setHint("Is Grayscale?");
        param->setAnimates(false);
        param->setDefault(false);
        param->setParent(*groupInput);
      }
      
      {
        OFX::StringParamDescriptor *param = desc.defineStringParam(kParamInputLensCalibrationFile(input));
        param->setLabel("Lens Calibration File");
        param->setHint("Calibration file to initialize camera intrinsics parameters.");
        param->setStringType(OFX::eStringTypeDirectoryPath);
        param->setParent(*groupInput);
      }

      //Lens calibration Group
      {
        OFX::GroupParamDescriptor *groupLensCalibration = desc.defineGroupParam(kParamInputGroupLensCalibration(input));
        groupLensCalibration->setLabel("Lens calibration");
        groupLensCalibration->setOpen(false);
        groupLensCalibration->setParent(*groupInput);

        {
          OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamInputSensorWidth(input));
          param->setLabel("Sensor Width");
          param->setHint("sensor Width"); //TODO FACA
          param->setDisplayRange(0, 100);
          param->setAnimates(false);
          param->setParent(*groupLensCalibration);
        }

        {
          OFX::Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamInputOpticalCenter(input));
          param->setLabel("Optical Center");
          param->setHint("Optical center coordinates");
          param->setDisplayRange(-50, -50, 50, 50);
          param->setAnimates(false);
          param->setUseHostOverlayHandle(false);
          param->setParent(*groupLensCalibration);
        }

        {
          OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamInputFocalLengthMode(input));
          param->setLabel("Focal Length Mode");
          param->setHint("Focal length information");
          param->setParent(*groupLensCalibration);
          param->appendOptions(kStringParamFocalLengthMode);
          param->setDefault(eParamFocalLengthModeKnown);
        }

        {
          OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamInputFocalLength(input));
          param->setLabel("Focal Length");
          param->setHint(
            "The focal length of the lens is the distance between the lens and "
            "the image sensor when the subject is in focus, usually stated in millimeters "
            "(e.g., 28 mm, 50 mm, or 100 mm). In the case of zoom lenses, both the "
            "minimum and maximum focal lengths are stated, for example 18–55 mm.\n"
            "The angle of view is the visible extent of the scene captured by the "
            "image sensor, stated as an angle. Wide angle of views capture greater "
            "areas, small angles smaller areas. Changing the focal length changes the "
            "angle of view. The shorter the focal length (e.g. 18 mm), the wider the "
            "angle of view and the greater the area captured. The longer the focal length "
            "(e.g. 55 mm), the smaller the angle and the larger the subject appears to be.");
          param->setDisplayRange(0, 300);
          param->setParent(*groupLensCalibration);
          param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        }

        {
          OFX::BooleanParamDescriptor *param = desc.defineBooleanParam(kParamInputFocalLengthVarying(input));
          param->setLabel("Varying");
          param->setHint("Is focal length varying?");
          param->setAnimates(false);
          param->setParent(*groupLensCalibration);
          param->setLayoutHint(OFX::eLayoutHintDivider);
        }

        {
          OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamInputDistortion(input));
          param->setLabel("Lens Distortion Status");
          param->setHint("Is the lens distortion calibrated?");
          param->setParent(*groupLensCalibration);
          param->appendOptions(kStringParamLensDistortion);
          param->setDefault(eParamLensDistortionKnown);
        }

        {
          OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamInputDistortionMode(input));
          param->setLabel("Lens Distortion Model");
          param->setHint("Mathematical model used for lens distortion.");
          param->setParent(*groupLensCalibration);
          param->appendOptions(kStringParamLensDistortionMode);
          param->setDefault(eParamLensDistortionModeRadial3);
        }

        {
          OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamInputDistortionCoef1(input));
          param->setLabel("Lens Distortion Coef1");
          param->setHint("Lens distortion coefficient 1");
          param->setRange(-1, 1);
          param->setDisplayRange(-1,1);
          param->setParent(*groupLensCalibration);
        }

        {
          OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamInputDistortionCoef2(input));
          param->setLabel("Lens Distortion Coef2");
          param->setHint("Lens distortion coefficient 2");
          param->setRange(-1, 1);
          param->setDisplayRange(-1,1);
          param->setParent(*groupLensCalibration);
        }

        {
          OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamInputDistortionCoef3(input));
          param->setLabel("Lens Distortion Coef3");
          param->setHint("Lens distortion coefficient 3");
          param->setRange(-1, 1);
          param->setDisplayRange(-1,1);
          param->setParent(*groupLensCalibration);
        }

        {
          OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamInputDistortionCoef4(input));
          param->setLabel("Lens Distortion Coef4");
          param->setHint("Lens distortion coefficient 4");
          param->setRange(-1, 1);
          param->setDisplayRange(-1,1);
          param->setParent(*groupLensCalibration);
        }
      }

      //Relative Pose Group
      {
        OFX::GroupParamDescriptor *groupRelativePose = desc.defineGroupParam(kParamInputGroupRelativePose(input));
        groupRelativePose->setLabel("Relative Pose");
        groupRelativePose->setOpen(false);
        groupRelativePose->setParent(*groupInput);

        {
          OFX::Double3DParamDescriptor *param = desc.defineDouble3DParam(kParamInputRelativePoseRotateM1(input));
          param->setLabel("Rotate");
          param->setHint("Relative pose rotation matrix 1");
          param->setDefault(0, 0, 0);
          param->setDisplayRange(-100, -100, -100, 100, 100, 100);
          param->setAnimates(false);
          param->setParent(*groupRelativePose);
        }

        {
          OFX::Double3DParamDescriptor *param = desc.defineDouble3DParam(kParamInputRelativePoseRotateM2(input));
          param->setLabel("");
          param->setHint("Relative pose rotation matrix 2");
          param->setDefault(0, 0, 0);
          param->setDisplayRange(-100, -100, -100, 100, 100, 100);
          param->setAnimates(false);
          param->setParent(*groupRelativePose);
        }      

        {
          OFX::Double3DParamDescriptor *param = desc.defineDouble3DParam(kParamInputRelativePoseRotateM3(input));
          param->setLabel("");
          param->setHint("Relative pose rotation matrix 3");
          param->setDefault(0, 0, 0);
          param->setDisplayRange(-100, -100, -100, 100, 100, 100);
          param->setAnimates(false);
          param->setParent(*groupRelativePose);
        }
        
        {
          OFX::Double3DParamDescriptor *param = desc.defineDouble3DParam(kParamInputRelativePoseCenter(input));
          param->setLabel("Center");
          param->setHint("Relative pose center");
          param->setDefault(0, 0, 0);
          param->setDisplayRange(-100, -100, -100, 100, 100, 100);
          param->setAnimates(false);
          param->setParent(*groupRelativePose);
        }
      }
    }
  }
  
  //Advanced Group
  {
    OFX::GroupParamDescriptor *groupAdvanced = desc.defineGroupParam(kParamGroupAdvanced);
    groupAdvanced->setLabel("Advanced");
    groupAdvanced->setAsTab();
    
    {
      OFX::BooleanParamDescriptor *param = desc.defineBooleanParam(kParamOverlay);
      param->setLabel("Overlay");
      param->setHint("Enable overlay of matched points and resection inliers/outliers.");
      param->setParent(*groupAdvanced);
      param->setDefault(false);
      param->setEvaluateOnChange(false);
    }

    {
      OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamAdvancedAlgorithm);
      param->setLabel("Algorithm");
      param->setHint("Camera Localizer Algorithm");
      param->setParent(*groupAdvanced);
      param->appendOptions(kStringParamAlgorithm);
      param->setDefault(eParamAlgorithmAllResults);
    }

    {
      OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamAdvancedReprojectionError);
      param->setLabel("Reprojection Error");
      param->setHint("Maximum reprojection error (in pixels) allowed for resectioning. If set to 0 it lets the ACRansac select an optimal value"); 
      param->setDisplayRange(0, 10);
      param->setDefault(4);
      param->setAnimates(false);
      param->setParent(*groupAdvanced);
    }
    
    {
      OFX::IntParamDescriptor *param = desc.defineIntParam(kParamAdvancedNbImageMatch);
      param->setLabel("Nb Image Match");
      param->setHint("Number of images to retrieve in database");
      param->setDisplayRange(0, 100);
      param->setDefault(4);
      param->setAnimates(false);
      param->setParent(*groupAdvanced);
    }
    
    {
      OFX::IntParamDescriptor *param = desc.defineIntParam(kParamAdvancedMaxResults);
      param->setLabel("Max Results");
      param->setHint("For algorithm AllResults, it stops the image matching when this number of matched images is reached. If 0 it is ignored.");
      param->setDisplayRange(0, 100);
      param->setDefault(10);
      param->setAnimates(false);
      param->setParent(*groupAdvanced);
    }
    
    {
      OFX::StringParamDescriptor *param = desc.defineStringParam(kParamAdvancedVoctreeWeights);
      param->setLabel("Voctree Weights File");
      param->setHint("Vocabulary tree weights filename"); 
      param->setStringType(OFX::eStringTypeDirectoryPath);
      param->setParent(*groupAdvanced);
    }
    
    {
      OFX::IntParamDescriptor *param = desc.defineIntParam(kParamAdvancedMatchingError);
      param->setLabel("Matching Error");
      param->setHint("Maximum matching error (in pixels) allowed for image matching with geometric verification. If set to 0 it lets the ACRansac select an optimal value.");
      param->setDisplayRange(0, 10);
      param->setDefault(4);
      param->setAnimates(false);
      param->setParent(*groupAdvanced);
    }
    
    {
      OFX::IntParamDescriptor *param = desc.defineIntParam(kParamAdvancedCctagNbNearestKeyFrames);
      param->setLabel("CCTag Nb Nearest KeyFrames");
      param->setHint("Number of images to retrieve in the database");
      param->setDisplayRange(0, 100);
      param->setAnimates(false);
      param->setDefault(5);
      param->setParent(*groupAdvanced);
    }
    
    {
      OFX::IntParamDescriptor *param = desc.defineIntParam(kParamAdvancedBaMinPointVisibility);
      param->setLabel("BA Min Point Visibility");
      param->setHint("Minimum number of observation that a point must have in order to be considered for bundle adjustment");
      param->setDisplayRange(0, 10);
      param->setDefault(0);
      param->setAnimates(false);
      param->setParent(*groupAdvanced);
    }
    
    {
      OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamAdvancedDistanceRatio);
      param->setLabel("Distance Ratio");
      param->setHint("The ratio distance to use when matching feature with the ratio test");
      param->setDisplayRange(-1,1);
      param->setDefault(0.8);
      param->setAnimates(false);
      param->setParent(*groupAdvanced);
    }
    
    {
      OFX::BooleanParamDescriptor *param = desc.defineBooleanParam(kParamAdvancedUseGuidedMatching);
      param->setLabel("Guided Matching");
      param->setHint("Use guided matching?");
      param->setAnimates(false);
      param->setDefault(false);
      param->setParent(*groupAdvanced);
    }
    
    {
      OFX::StringParamDescriptor *param = desc.defineStringParam(kParamAdvancedDebugFolder);
      param->setLabel("Debug Folder");
      param->setHint("If a directory is provided it enables visual debug and saves all the debugging info in that directory");
      param->setStringType(OFX::eStringTypeDirectoryPath);
      param->setParent(*groupAdvanced);
    }
    
    {
      OFX::BooleanParamDescriptor *param = desc.defineBooleanParam(kParamAdvancedDebugAlwaysComputeFrame);
      param->setLabel("Always Compute Frame");
      param->setHint("Is always computing frame?");
      param->setAnimates(false);
      param->setDefault(false);
      param->setParent(*groupAdvanced);
    }
  }
  
  //Tracking Group
  {
    OFX::GroupParamDescriptor *groupTracking = desc.defineGroupParam(kParamGroupTracking);
    groupTracking->setLabel("Tracking");
    groupTracking->setAsTab();
    
    {
      OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamTrackingRangeMode);
      param->setLabel("Tracking Range");
      param->setHint("Tracking Range");
      param->appendOptions(kStringParamTrackingRangeMode);
      param->setEvaluateOnChange(false);
      param->setEnabled(true);
      param->setParent(*groupTracking);
    }

    {
      OFX::IntParamDescriptor *param = desc.defineIntParam(kParamTrackingRangeMin);
      param->setLabel("from");
      param->setHint("Start frame");
      param->setDisplayRange(0, 240);
      param->setAnimates(false);
      param->setEvaluateOnChange(false);
      param->setEnabled(false);
      param->setParent(*groupTracking);
    }

    {
      OFX::IntParamDescriptor *param = desc.defineIntParam(kParamTrackingRangeMax);
      param->setLabel("to");
      param->setHint("Stop frame");
      param->setDisplayRange(0, 240);
      param->setAnimates(false);
      param->setEvaluateOnChange(false);
      param->setEnabled(false);
      param->setParent(*groupTracking);
    }

    {
      OFX::PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamTrackingTrack);
      param->setLabel("Track");
      param->setHint("track");
      param->setEnabled(false);
      param->setParent(*groupTracking);
    }
  }

  //Camera Output Group
  {
    OFX::GroupParamDescriptor *groupOutput = desc.defineGroupParam(kParamGroupOutput);
    groupOutput->setLabel("Output");
    groupOutput->setAsTab();
    
    {
      OFX::Double3DParamDescriptor *param = desc.defineDouble3DParam(kParamOutputTranslate);
      param->setLabel("Translate");
      param->setHint("Camera output translate");
      param->setAnimates(true);
      param->setEnabled(false);
      param->setEvaluateOnChange(false);
      param->setCanUndo(false);
      param->setParent(*groupOutput);
    }

    {
      OFX::Double3DParamDescriptor *param = desc.defineDouble3DParam(kParamOutputRotate);
      param->setLabel("Rotate");
      param->setHint("Camera output rotate");
      param->setAnimates(true);
      param->setEnabled(false);
      param->setEvaluateOnChange(false);
      param->setCanUndo(false);
      param->setParent(*groupOutput);
    }

    {
      OFX::Double3DParamDescriptor *param = desc.defineDouble3DParam(kParamOutputScale);
      param->setLabel("Scale");
      param->setHint("Camera output scale");
      param->setAnimates(true);
      param->setEnabled(false);
      param->setEvaluateOnChange(false);
      param->setCanUndo(false);
      param->setParent(*groupOutput);
    }
    
    {
      OFX::Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamOutputOpticalCenter);
      param->setLabel("Optical Center");
      param->setHint("Camera output optical center");
      param->setDisplayRange(-50, -50, 50, 50);
      param->setAnimates(true);
      param->setEnabled(false);
      param->setUseHostOverlayHandle(false);
      param->setEvaluateOnChange(false);
      param->setCanUndo(false);
      param->setParent(*groupOutput);
    }
    
    {
      OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamOutputFocalLength);
      param->setLabel("Focal Length");
      param->setHint("Camera output focal length");
      param->setDisplayRange(0, 300);
      param->setAnimates(true);
      param->setEnabled(false);
      param->setEvaluateOnChange(false);
      param->setCanUndo(false);
      param->setParent(*groupOutput);
    }
    
    {
      OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamOutputNear);
      param->setLabel("Near");
      param->setHint("Camera output near distance");
      param->setDisplayRange(0, 300);
      param->setAnimates(true);
      param->setEnabled(false);
      param->setEvaluateOnChange(false);
      param->setCanUndo(false);
      param->setParent(*groupOutput);
    }
    
    {
      OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamOutputFar);
      param->setLabel("Far");
      param->setHint("Camera output far distance");
      param->setDisplayRange(0, 300);
      param->setAnimates(true);
      param->setEnabled(false);
      param->setEvaluateOnChange(false);
      param->setCanUndo(false);
      param->setParent(*groupOutput);
    }
    
    {
      OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamOutputDistortionCoef1);
      param->setLabel("Lens Distortion Coef1");
      param->setHint("Lens distortion coefficient 1");
      param->setRange(-1, 1);
      param->setDisplayRange(-1,1);
      param->setEnabled(false);
      param->setEvaluateOnChange(false);
      param->setCanUndo(false);
      param->setParent(*groupOutput);
    }

    {
      OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamOutputDistortionCoef2);
      param->setLabel("Lens Distortion Coef2");
      param->setHint("Lens distortion coefficient 2");
      param->setRange(-1, 1);
      param->setDisplayRange(-1,1);
      param->setEnabled(false);
      param->setEvaluateOnChange(false);
      param->setCanUndo(false);
      param->setParent(*groupOutput);
    }

    {
      OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamOutputDistortionCoef3);
      param->setLabel("Lens Distortion Coef3");
      param->setHint("Lens distortion coefficient 3");
      param->setRange(-1, 1);
      param->setDisplayRange(-1,1);
      param->setEnabled(false);
      param->setEvaluateOnChange(false);
      param->setCanUndo(false);
      param->setParent(*groupOutput);
    }

    {
      OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamOutputDistortionCoef4);
      param->setLabel("Lens Distortion Coef4");
      param->setHint("Lens distortion coefficient 4");
      param->setRange(-1, 1);
      param->setDisplayRange(-1,1);
      param->setEnabled(false);
      param->setEvaluateOnChange(false);
      param->setCanUndo(false);
      param->setParent(*groupOutput);
      param->setLayoutHint(OFX::eLayoutHintDivider);
    }
    
    {
      OFX::GroupParamDescriptor *groupError = desc.defineGroupParam(kParamOutputStatGroup);
      groupError->setLabel("Error Statistics");
      groupError->setParent(*groupOutput);
      groupError->setOpen(false);

      {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamOutputStatErrorMean);
        param->setLabel("Error Mean");
        param->setHint("Error Mean");
        param->setDisplayRange(-1,1);
        param->setEnabled(false);
        param->setEvaluateOnChange(false);
        param->setCanUndo(false);
        param->setParent(*groupError);
      }
      
      {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamOutputStatErrorMin);
        param->setLabel("Error Min");
        param->setHint("Error Min");
        param->setDisplayRange(-1,1);
        param->setEnabled(false);
        param->setEvaluateOnChange(false);
        param->setCanUndo(false);
        param->setParent(*groupError);
      }
      
      {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamOutputStatErrorMax);
        param->setLabel("Error Max");
        param->setHint("Error Max");
        param->setDisplayRange(-1,1);
        param->setEnabled(false);
        param->setEvaluateOnChange(false);
        param->setCanUndo(false);
        param->setParent(*groupError);
      }
      
      {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamOutputStatNbMatchedImages);
        param->setLabel("Nb Matched Images");
        param->setHint("Number of images matched in the 3D reconstruction.");
        param->setDisplayRange(0, 50);
        param->setEnabled(false);
        param->setEvaluateOnChange(false);
        param->setCanUndo(false);
        param->setParent(*groupError);
      }
      
      {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamOutputStatNbDetectedFeatures);
        param->setLabel("Nb Detected Features");
        param->setHint("Number of features detected in the image.");
        param->setDisplayRange(0, 50000);
        param->setEnabled(false);
        param->setEvaluateOnChange(false);
        param->setCanUndo(false);
        param->setParent(*groupError);
      }
      
      {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamOutputStatNbMatchedFeatures);
        param->setLabel("Nb Matched Features");
        param->setHint("Number of features from the image that match with 3D points.");
        param->setDisplayRange(0, 5000);
        param->setEnabled(false);
        param->setEvaluateOnChange(false);
        param->setCanUndo(false);
        param->setParent(*groupError);
      }
      
      {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamOutputStatNbInlierFeatures);
        param->setLabel("Nb Inliers Features");
        param->setHint("Number of features validated / used for the camera pose localization.");
        param->setDisplayRange(0, 2000);
        param->setEnabled(false);
        param->setEvaluateOnChange(false);
        param->setCanUndo(false);
        param->setParent(*groupError);

        param->setLayoutHint(OFX::eLayoutHintDivider); // Next section
      }
    }

    {
      OFX::PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamOutputClearCurrentFrame);
      param->setLabel("Clear Current Frame");
      param->setHint("Clear current frame values and cache");
      param->setEnabled(true);
      param->setParent(*groupOutput);
    }

    {
      OFX::PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamOutputClear);
      param->setLabel("Clear All");
      param->setHint("Clear all output values and cache");
      param->setEnabled(true);
      param->setParent(*groupOutput);
    }

    {
      OFX::PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamOutputCreateCamera);
      param->setLabel("Create Camera");
      param->setHint("Create a linked Nuke camera");
      param->setEnabled(true);
      param->setParent(*groupOutput);
    }
  }
  
  {
    OFX::IntParamDescriptor *param = desc.defineIntParam(kParamForceInvalidation);
    param->setLabel("Force Invalidation");
    param->setHint("Allow the plugin to force the host to render");
    param->setIsSecret(true);
    param->setEnabled(false);
    param->setCanUndo(false);
  }
  
  {
    OFX::IntParamDescriptor *param = desc.defineIntParam(kParamForceInvalidationAtTime);
    param->setLabel("Force Invalidation At Time");
    param->setHint("Allow the plugin to force the host to render at a specific time");
    param->setIsSecret(true);
    param->setEnabled(false);
    param->setCanUndo(false);
  }
}

OFX::ImageEffect* CameraLocalizerPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
  return new CameraLocalizerPlugin(handle);
}


} //namespace Localizer
} //namespace openMVG_ofx
