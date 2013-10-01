
// MadronaLib: a C++ framework for DSP applications.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

// Portions of this software originate from JUCE, 
// copyright 2004-2013 by Raw Material Software ltd.
// JUCE is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "MLDial.h"
#include "MLLookAndFeel.h"

static const int kMinPixelMovement = 4;
const int kDragStepSize = 2;
const int kMouseWheelStepSize = 2;
static const int kMinimumDialSizeForJump = 26;
static const int kLabelHeight = 16;
static const int kSmallLabelHeight = 11;
static const float kSpeedThresh = 4.;
static const float kSpeedMax = 32.;
static const float kRotaryStartDefault = kMLPi*-0.75f;
static const float kRotaryEndDefault = kMLPi*0.5f;

//==============================================================================
MLDial::MLDial () : 
	dialBeingDragged (NoDial),
	dialToDrag (NoDial),
	//
    currentValue (0.0), valueMin (0.0), valueMax (0.0),
    minimum (0), maximum (1), interval (0), doubleClickReturnValue(0.0),
	valueWhenLastDragged(0), valueOnMouseDown(0),
    numDecimalPlaces (7),
 	mOverTrack(false),
	//
	mLastDragTime(0), mLastWheelTime(0),
	mLastDragX(0), mLastDragY(0),
	mFilteredMouseSpeed(0.),
	mMouseMotionAccum(0),
	//
	mHilighted(false), mWasHilighted(false),
    pixelsForFullDragExtent (250),
    style (MLDial::LinearHorizontal),
	mValueDisplayMode(eMLNumFloat),
	//
    doubleClickToValue (false),
    isVelocityBased (false),
    userKeyOverridesVelocity (true),
    rotaryStop (true),
    incDecButtonsSideBySide (false),
    sendChangeOnlyOnRelease (false),
    popupDisplayEnabled (false),
    menuShown (false),
	mouseWasHidden(false),	
    scrollWheelEnabled (true),
    snapsToMousePos (true),
	//
	mWarpMode (kJucePluginParam_Linear),
	mZeroThreshold(0. - (2<<16)),
	mTopLeft(false),
	mDrawThumb(true),
	mDoSign(false),
	mDoNumber(true),
	mDigits(3), mPrecision(2),
	mBipolar(false),
	mTrackThickness(kMLTrackThickness),
	mTicks(2),
	mTicksOffsetAngle(0.),
	mDiameter(0),
	mMargin(0),
	mTickSize(0),
	mShadowSize(0),
	mTextSize(0.),
	mMaxNumberWidth(0.),
	//
	mSnapToDetents(true),
	mCurrentDetent(-1),
	mPrevLFDrawNumbers(false), // TODO
	//
	mParameterLayerNeedsRedraw(true),
	mStaticLayerNeedsRedraw(true),		
	mThumbLayerNeedsRedraw(true),
	mpListener(0)
{
	MLWidget::setComponent(this);
	MLLookAndFeel* myLookAndFeel = MLLookAndFeel::getInstance();
	setOpaque(myLookAndFeel->getDefaultOpacity());
    
    // TODO sort out issues with Retina and buffering
	setBufferedToImage(myLookAndFeel->getDefaultBufferMode());

	setPaintingIsUnclipped(myLookAndFeel->getDefaultUnclippedMode());

	setWantsKeyboardFocus (false);
    setRepaintsOnMouseActivity (false);
	
	setRotaryParameters(kRotaryStartDefault, kRotaryEndDefault, true);
	setVelocityBasedMode(true);
}

MLDial::~MLDial()
{
}

void MLDial::setListener (MLDial::Listener* const l)
{
    assert (l);
    mpListener = l;
}

void MLDial::setAttribute(MLSymbol attr, float val)
{
	static const MLSymbol valueSym("value");
	MLWidget::setAttribute(attr, val);
	
//debug() << "MLDial " << getWidgetName() << ":" << attr << " = " << val << "\n";
	
	// TODO other dial attributes?
	bool quiet = true;
	
	if (attr == valueSym)
	{
		setValueOfDial(MainDial, val, quiet);
	}
}

//--------------------------------------------------------------------------------
// AsyncUpdater methods

void MLDial::handleAsyncUpdate()
{
	if(mpListener)
	{
		cancelPendingUpdate();
		mpListener->dialValueChanged (this);
	}
}

//--------------------------------------------------------------------------------

void MLDial::sendDragStart()
{
    if(mpListener)
        mpListener->dialDragStarted (this);
}

void MLDial::sendDragEnd()
{
    if(mpListener)
        mpListener->dialDragEnded (this);
}

// TODO use attributes
void MLDial::setDialStyle (const MLDial::DialStyle newStyle)
{
    if (style != newStyle)
    {
        style = newStyle;
        lookAndFeelChanged();
    }
}

// TODO use attributes
void MLDial::setRotaryParameters (const float startAngleRadians,
                                  const float endAngleRadians,
                                  const bool stopAtEnd)
{
    // make sure the values are sensible..
//    jassert (startAngleRadians >= 0 && endAngleRadians >= 0);
//    jassert (startAngleRadians < float_Pi * 4.0f && rotaryEnd < float_Pi * 4.0f);
//    jassert (startAngleRadians < rotaryEnd);

	float start = startAngleRadians;
	float end = endAngleRadians;

	while(start < 0.)
	{
		start += 2*kMLPi;
	}
	while(end < start)
	{
		end += 2*kMLPi;
	}

    rotaryStart = start;
    rotaryEnd = end;
    rotaryStop = stopAtEnd;
}

void MLDial::setVelocityBasedMode (const bool velBased) throw()
{
    isVelocityBased = velBased;
}

void MLDial::setMouseDragSensitivity (const int distanceForFullScaleDrag)
{
    jassert (distanceForFullScaleDrag > 0);

    pixelsForFullDragExtent = distanceForFullScaleDrag;
}

void MLDial::setChangeNotificationOnlyOnRelease (const bool onlyNotifyOnRelease) throw()
{
    sendChangeOnlyOnRelease = onlyNotifyOnRelease;
}

void MLDial::setDialSnapsToMousePosition (const bool shouldSnapToMouse) throw()
{
    snapsToMousePos = shouldSnapToMouse;
}

/*
void MLDial::setPopupDisplayEnabled (const bool enabled,
                                     Component* const parentComponentToUse) throw()
{
    popupDisplayEnabled = enabled;
    parentForPopupDisplay = parentComponentToUse;
}
*/

//==============================================================================
void MLDial::colourChanged()
{
    lookAndFeelChanged();
}

void MLDial::lookAndFeelChanged()
{
    repaintAll();
}


	
//==============================================================================

void MLDial::setWarpMode(const JucePluginParamWarpMode w)
{	
	mWarpMode = w;
}


void MLDial::setRange (const float newMin,
                       const float newMax,
                       const float newInt,
                       const float zeroThresh,
					   JucePluginParamWarpMode warpMode)
{
	minimum = newMin;
	maximum = newMax;
	interval = newInt;
	mZeroThreshold = zeroThresh;
	mWarpMode = warpMode;

	// figure out the number of decimal places needed to display all values at this
	// interval setting.
	numDecimalPlaces = 7;
	if (newInt != 0)
	{
		int v = abs ((int) (newInt * 10000000));
		while ((v % 10) == 0)
		{
			--numDecimalPlaces;
			v /= 10;
		}
	}

	// keep the current values inside the new range..
	if (style != MLDial::TwoValueHorizontal && style != MLDial::TwoValueVertical)
	{
		setSelectedValue (currentValue, mainValue, false, false);
	}
	else
	{
		setSelectedValue (getMinValue(), minValue, false, false);
		setSelectedValue (getMaxValue(), maxValue, false, false);
	}
	
	mDigits = ceil(log10(jmax((abs(newMin) + 1.), (abs(newMax) + 1.))));
	mDoSign = ((newMax < 0) || (newMin < 0));
	mPrecision = ceil(log10(1. / newInt) - 0.0001);	
	
	// debug() << getWidgetName() << "SET RANGE: digits: " << mDigits << " precision: " << 	mPrecision << " sign: " << mDoSign << "\n";
	// debug() << "PRECISION:" << mPrecision << ", INTERVAL:" << newInt << "\n";

}

void MLDial::setDefault (const float newDefault)
{
	setDoubleClickReturnValue(true, newDefault);
}

void MLDial::triggerChangeMessage (const bool synchronous)
{
    if (synchronous)
        handleAsyncUpdate();
    else
        triggerAsyncUpdate();

    valueChanged();
}

float MLDial::getValue() const throw()
{
    // for a two-value style MLDial, you should use the getMinValue() and getMaxValue()
    // methods to get the two values.
    jassert (style != MLDial::TwoValueHorizontal && style != MLDial::TwoValueVertical);

    return currentValue;
}

float MLDial::getMinValue() const throw()
{
    jassert (isTwoOrThreeValued());
	return valueMin;
}

float MLDial::getMaxValue() const throw()
{
    jassert (isTwoOrThreeValued());
    return valueMax;
}

float MLDial::getValueOfDial(WhichDial s)
{
	float val = 0.;
	switch(s) 
	{
		case MainDial:
			val = currentValue;
			break;
		case MinDial:
			val = valueMin;
			break;
		case MaxDial:
			val = valueMax;
			break;		
		default:
			break;		
	}
	return val;
}


void MLDial::setValueOfDial(WhichDial s, float val, bool quiet)
{
	bool sendMsg = !quiet;
	bool synch = false;
	float newVal = snapValue(val, true);
	switch(s) 
	{
		case MainDial:
            setSelectedValue (newVal, mainValue, sendMsg, synch);
			break;
		case MinDial:
            setSelectedValue (newVal, minValue, sendMsg, synch);
			break;
		case MaxDial:
            setSelectedValue (newVal, maxValue, sendMsg, synch);
			break;		
		default:
			break;		
	}
}

void MLDial::setSelectedValue (float newValue,
					const int valSelector,
					const bool sendUpdateMessage,
					const bool sendMessageSynchronously)
{	
//debug() << "in constrain: " << newValue << "\n";
	newValue = constrainedValue (newValue);
//debug() << "    out constrain: " << newValue << "\n";
	
	// get reference to target
	float* pTargetValue = 0;
	switch(valSelector)
	{
		default:
		case mainValue:
			pTargetValue = &currentValue;
		break;
		case minValue:
			pTargetValue = &valueMin;
		break;
		case maxValue:
			pTargetValue = &valueMax;
		break;
	}

	// clip value to other dial parts, but only if we are initiating change from the dial. 
	if (sendUpdateMessage)
	{
		switch(valSelector)
		{
			case mainValue:
				if (style == MLDial::ThreeValueHorizontal || style == MLDial::ThreeValueVertical)
				{
					jassert (valueMin <= valueMax);
					newValue = jlimit (valueMin, valueMax, newValue);
				}
			break;
			case minValue:
				if (style == MLDial::TwoValueHorizontal || style == MLDial::TwoValueVertical)
					newValue = jmin (valueMax, newValue);
				else
					newValue = jmin (currentValue, newValue);
			break;
			case maxValue:
				if (style == MLDial::TwoValueHorizontal || style == MLDial::TwoValueVertical)
					newValue = jmax (valueMin, newValue);
				else
					newValue = jmax (currentValue, newValue);
			break;
		}
	}

    if (*pTargetValue != newValue)
    {
		// debug
		if (newValue != newValue) 
		{
			debug() << "value NaN!\n";
		}
	
		*pTargetValue = newValue;
		mParameterLayerNeedsRedraw = true;
		mThumbLayerNeedsRedraw = true;
		
        if (sendUpdateMessage)
            triggerChangeMessage (sendMessageSynchronously);

		repaintAll();
    }
}

void MLDial::setDoubleClickReturnValue (const bool isDoubleClickEnabled,
                                        const float valueToSetOnDoubleClick) throw()
{
    doubleClickToValue = isDoubleClickEnabled;
    doubleClickReturnValue = valueToSetOnDoubleClick;
}

float MLDial::getDoubleClickReturnValue (bool& isEnabled_) const throw()
{
    isEnabled_ = doubleClickToValue;
    return doubleClickReturnValue;
}

const String MLDial::getTextFromValue (float v)
{
    if (numDecimalPlaces > 0)
        return String (v, numDecimalPlaces) + textSuffix;
    else
        return String (roundDoubleToInt (v)) + textSuffix;
}

float MLDial::getValueFromText (const String& text)
{
    String t (text.trimStart());

    if (t.endsWith (textSuffix))
        t = t.substring (0, t.length() - textSuffix.length());

    while (t.startsWithChar ('+'))
        t = t.substring (1).trimStart();

    return t.initialSectionContainingOnly ("0123456789.,-")
            .getDoubleValue();
}

// TODO get value using Parameter
float MLDial::proportionOfLengthToValue (float proportion)
{
	float min = getMinimum();
	float max = getMaximum();
	float r, rangeExp;
	float p = proportion;
	bool flip = false;

	if (min > max)
	{
		float temp = max;
		max = min;
		min = temp;
		flip = true;
	}
	
	if (flip)
	{
		p = 1. - p;
	}

	if (mWarpMode == kJucePluginParam_Exp)
	{
		rangeExp = p*(log(max)/log(min) - 1) + 1;
		r = pow(min, rangeExp);
		if (r < mZeroThreshold)
		{
			r = 0.;
		}		
	}
	else
	{
		r = minimum + (maximum - minimum) * proportion;
	}

	return r;
}

// TODO get value using Parameter
float MLDial::valueToProportionOfLength (float value) const
{
	float min = getMinimum();
	float max = getMaximum();
	float x;
	bool flip = false;
	
	if (min > max)
	{
		float temp = max;
		max = min;
		min = temp;
		flip = true;
	}
		
	if (mWarpMode == kJucePluginParam_Exp)
	{
		value = clamp(value, min, max);
		x = log(value/min) / log(max/min);
	}
	else
	{
		x = (value - minimum) / (maximum - minimum);
	}
	
	if (flip)
	{
		x = 1. - x;
	}

	return x;
}

//==============================================================================
#pragma mark -

float MLDial::snapValue (float attemptedValue, const bool)
{
	float r = 0.;
	if (attemptedValue != attemptedValue)
	{
		MLError() << "dial " << getName() << ": not a number!\n";
	}
	else 
	{
		r = clamp(attemptedValue, minimum, maximum);
	}

	return r;
}

unsigned MLDial::nearestDetent (float attemptedValue) const
{
	unsigned r = attemptedValue;
	
	int detents = mDetents.size();	
	if (detents)
	{
		float p1 = valueToProportionOfLength(attemptedValue);
		int i_min = 0;
		float d_min = 9999999.;
		
		for (int i=0; i<detents; ++i)
		{
			float td = fabs(valueToProportionOfLength(mDetents[i].mValue) - p1);
			if (td < d_min)
			{
				i_min = i;
				d_min = td;
			}			
		}
		r = i_min;
	}
	return r;
}

//==============================================================================

void MLDial::valueChanged()
{
	mParameterLayerNeedsRedraw = true;
	mThumbLayerNeedsRedraw = true;
}

//==============================================================================
void MLDial::enablementChanged()
{
	repaintAll();
}

void MLDial::setScrollWheelEnabled (const bool enabled) throw()
{
    scrollWheelEnabled = enabled;
}

//==============================================================================
// all value changes should pass through here.
// 
float MLDial::constrainedValue (float value) const throw()
{
	float min = minimum;
	float max = maximum;
	bool flip = false;
	if (minimum > maximum)
	{
		float temp = max;
		max = min;
		min = temp;
		flip = true;
	}

	// quantize to chunks of interval
	int detents = mDetents.size();	
    if ((interval > 0) && (!detents))
	{
        value = min + interval * floor((value - min)/interval + 0.5);
	}
	value = clamp(value, min, max);
	if (value <= mZeroThreshold)
	{
		value = 0.;
	}

    return value;
}

float MLDial::getLinearDialPos (const float value)
{
    float dialPosProportional;
	float ret = 0.;
    if (maximum > minimum)
    {
        if (value < minimum)
        {
            dialPosProportional = 0.0;
        }
        else if (value > maximum)
        {
            dialPosProportional = 1.0;
        }
        else
        {
            dialPosProportional = valueToProportionOfLength (value);
            jassert (dialPosProportional >= 0 && dialPosProportional <= 1.0);
        }
    }
    else
    {
        dialPosProportional = 0.5;
    }

	float start, extent;
    if (isVertical())
	{
        dialPosProportional = 1.0 - dialPosProportional;
		start = trackRect.top() - 1.f;
		extent = trackRect.height() + 1.f;
	}
	else
	{
		start = trackRect.left() - 1.f;
		extent = trackRect.width() + 1.f;
	}
	ret = (float) (start + dialPosProportional*extent);
    return ret;
}

bool MLDial::isHorizontal() const throw()
{
    return style == MLDial::LinearHorizontal
        || style == MLDial::LinearBar
        || style == MLDial::TwoValueHorizontal
        || style == MLDial::ThreeValueHorizontal
        || style == MLDial::MultiHorizontal;
}

bool MLDial::isVertical() const throw()
{
    return style == MLDial::LinearVertical
        || style == MLDial::TwoValueVertical
        || style == MLDial::ThreeValueVertical
        || style == MLDial::MultiVertical;
}

bool MLDial::isTwoOrThreeValued() const throw()
{
    return style == MLDial::TwoValueVertical
        || style == MLDial::ThreeValueVertical
        || style == MLDial::TwoValueHorizontal
        || style == MLDial::ThreeValueHorizontal;
}

bool MLDial::isTwoValued() const throw()
{
    return style == MLDial::TwoValueVertical
        || style == MLDial::TwoValueHorizontal;
}

bool MLDial::isMultiValued() const throw()
{
    return style == MLDial::MultiVertical
        || style == MLDial::MultiHorizontal;
}


float MLDial::getPositionOfValue (const float value)
{
    if (isHorizontal() || isVertical())
    {
        return getLinearDialPos (value);
    }
    else
    {
        jassertfalse // not a valid call on a MLDial that doesn't work linearly!
        return 0.0f;
    }
}

#pragma mark -
#pragma mark paint
//==============================================================================


void MLDial::paint (Graphics& g)
{
	enterPaint();
	const int width = getWidth();
	const int height = getHeight();

	MLLookAndFeel* myLookAndFeel = MLLookAndFeel::getInstance();

	// TODO draw backgroun dbehind number
	if (isOpaque()) 
		myLookAndFeel->drawBackground(g, this);	
	
	// see if lookandfeel's drawNumbers changed
	bool LFDrawNumbers = myLookAndFeel->mDrawNumbers;
	if (LFDrawNumbers != mPrevLFDrawNumbers)
	{
		mParameterLayerNeedsRedraw = true;
		mThumbLayerNeedsRedraw = true;
		mPrevLFDrawNumbers = LFDrawNumbers;
	}
	
	if (style == MLDial::Rotary)
	{
		const float dialPos = (float) valueToProportionOfLength (currentValue);
		drawRotaryDial(g, 0, 0, width, height, dialPos);
	}
	else
	{
		drawLinearDial(g, 0, 0, width, height,  
			getLinearDialPos(currentValue), getLinearDialPos(valueMin), getLinearDialPos(valueMax),
			style);
	}

	/*
	// TEST
	Path P;
	const Rectangle<int> & br ( getLocalBounds());	
//	P.addRectangle(br);
	P.addRectangle(MLToJuceRectInt(mRotaryTextRect));
	g.setColour(Colours::red);	
	g.strokePath(P, PathStrokeType(0.5f));
	*/
}

void MLDial::repaintAll()
{
//	int width = 0;//getWidth();
//	int height = 0;//getHeight();
//	mSignalRedrawBounds.setBounds(0, 0, width, height);
	mParameterLayerNeedsRedraw = mThumbLayerNeedsRedraw = mStaticLayerNeedsRedraw = true;
	repaint();
}

void MLDial::drawLinearDial (Graphics& g, int , int , int , int ,
	float dialPos, float minDialPos, float maxDialPos,
    const MLDial::DialStyle )
{
	const int compWidth = getWidth();
	const int compHeight = getHeight();
	MLLookAndFeel* myLookAndFeel = MLLookAndFeel::getInstance();
	
	// COLORS
	
	Path full, not_full, thumb;	
	MLRect sr(getWidgetLocalBounds());
	
	const int multi = (isTwoOrThreeValued());
	//const int textSize = mTextSize;
	
	const float cornerSize = 2.;
	int thumbAdorn1, thumbAdorn2;
//	int currX, currY;	
	float val1, val2;
	long doDial1, doDial2;		

	const Colour trackDark (mTrackDarkColor);		
	const Colour trackColour (myLookAndFeel->shadowColor);		
	const Colour outline = findColour(MLLookAndFeel::outlineColor).withAlpha (isEnabled() ? 1.f : 0.5f);
	const Colour textColor = outline;
	const Colour shadow = findColour(MLLookAndFeel::shadowColor).withAlpha (isEnabled() ? 1.f : 0.5f);
	const Colour fill_glow (mGlowColor.withAlpha (isEnabled() ? 1.f : 0.5f));
	const Colour fill_normal = (mTrackFillColor.withAlpha (isEnabled() ? 1.f : 0.5f));
	const Colour thumb_normal = (mFillColor.withAlpha (isEnabled() ? 1.f : 0.5f));
	const Colour thumb_glow (mThumbGlowColor.withAlpha (isEnabled() ? 1.f : 0.5f));
	const Colour signal_color (mIndicatorColor.withAlpha (isEnabled() ? 1.f : 0.f));
		
	// get glows
	// DIMENSIONS
	int glowTrack = false, glow1 = false, glow2 = false;
	
 	if (multi)
	{
		doDial1 = doDial2 = true;
		val1 = getMinValue();
		val2 = getMaxValue();
	}
	else
	{
		doDial1 = mTopLeft;
		doDial2 = !doDial1;
		val1 = val2 = getValue();
	}
	
	if (isHorizontal()) 
	{
		thumbAdorn1 = eMLAdornBottomLeft | eMLAdornBottom | eMLAdornBottomRight;
		thumbAdorn2 = eMLAdornTopLeft | eMLAdornTop | eMLAdornTopRight;
	}
	else // vertical
	{
		thumbAdorn1 = eMLAdornTopRight | eMLAdornRight | eMLAdornBottomRight;	
		thumbAdorn2 = eMLAdornTopLeft | eMLAdornLeft | eMLAdornBottomLeft;
	}
	
	MLRect nfr1, nfr2, fr, tr, thumb1, thumb2, tip1, tip2, text1, text2;
	getDialRect (nfr1, MLDial::NoFillRect1, dialPos, minDialPos, maxDialPos);
	getDialRect (nfr2, MLDial::NoFillRect2, dialPos, minDialPos, maxDialPos);
	getDialRect (fr, MLDial::FillRect, dialPos, minDialPos, maxDialPos);
	getDialRect (tr, MLDial::TrackRect, dialPos, minDialPos, maxDialPos);
	getDialRect (thumb1, MLDial::Thumb1Rect, dialPos, minDialPos, maxDialPos);
	getDialRect (thumb2, MLDial::Thumb2Rect, dialPos, minDialPos, maxDialPos);
	getDialRect (tip1, MLDial::Tip1Rect, dialPos, minDialPos, maxDialPos);
	getDialRect (tip2, MLDial::Tip2Rect, dialPos, minDialPos, maxDialPos);
	getDialRect (text1, MLDial::Text1Rect, dialPos, minDialPos, maxDialPos);
	getDialRect (text2, MLDial::Text2Rect, dialPos, minDialPos, maxDialPos);
	
	full.addRectangle(MLToJuceRect(fr));
	
	// DRAWING	
	
	// parameter layer
	if (mParameterLayerNeedsRedraw)
	{	
		Graphics pg(mParameterImage);	
		mParameterImage.clear(Rectangle<int>(0, 0, compWidth, compHeight), Colours::transparentBlack);
		
		// track unfilled area(s)
		pg.setColour (trackDark);
		pg.fillRect (MLToJuceRectInt(nfr1));	
		pg.fillRect (MLToJuceRectInt(nfr2));	
		
		// outer shadow
		Path sp;
		float d, opacity;
		for (int i=1; i<=mShadowSize; i++)
		{
			sp.clear();			
			MLRect r = tr.getIntPart();
			r.expand(i*2);
			sp.addRectangle(MLToJuceRect(r));
			d = (float)(mShadowSize - i) / (float)mShadowSize; // 0. - 1.
			opacity = d * d * d * kMLShadowOpacity;
			pg.setColour (shadow.withAlpha(opacity));
			pg.strokePath (sp, PathStrokeType (1.f));	
		}
		
		// draw fill on top of shadow
		{
			pg.setColour (glowTrack ? fill_glow : fill_normal);  // fill could be gradient
			pg.fillPath (full);	
		}
		
		// draw track outline
		{
			Path track;
			MLRect tra = tr.getIntPart();
			tra.expand(1.f);
			track.addRectangle (MLToJuceRect(tra)); 
			pg.setColour (outline);
			pg.strokePath (track, PathStrokeType(mLineThickness));
		}
		
	}
	
	// static layer
	if (mStaticLayerNeedsRedraw)
	{
		Graphics sg(mStaticImage);	
		mStaticImage.clear(Rectangle<int>(0, 0, compWidth, compHeight), Colours::transparentBlack);
		const Colour label_color = (findColour(MLLookAndFeel::labelColor).withAlpha (isEnabled() ? 1.f : 0.5f));	
	
		// detents 
        if (isHorizontal())
        {
			float tX = tr.x();
			float tY = tr.y();
			float tW = tr.getWidth();
			float x1, y1, x2, y2;

			for (unsigned i=0; i<mDetents.size(); ++i)
			{
				float td = valueToProportionOfLength(mDetents[i].mValue); 
				float xx = (tX + (td * tW));	
				Path J;

				// draw tick
				x1 = xx;
				y1 = tY - mMargin;
				x2 = xx;
				y2 = tY - mMargin*(1.0f - mDetents[i].mWidth);
									
				J.startNewSubPath(x1, y1);
				J.lineTo(x2, y2);

				sg.setColour (label_color.withAlpha(1.f));
				sg.strokePath (J, PathStrokeType(mLineThickness*0.75f));
 
			}
		}
        else
        {
 			float tX = tr.x();
			float tY = tr.y();
			float tW = tr.getWidth();
			float tH = tr.getHeight();
			float x1, y1, x2, y2;
            
			for (unsigned i=0; i<mDetents.size(); ++i)
			{
				float td = valueToProportionOfLength(mDetents[i].mValue);
				float yy = (tY + (td * tH));
				Path J;
                
				// draw tick
				x1 = tX - mMargin;
				y1 = yy;
				x2 = tX - mMargin*(1.0f - mDetents[i].mWidth);
				y2 = yy;
                
				J.startNewSubPath(x1, y1);
				J.lineTo(x2, y2);
                
				sg.setColour (label_color.withAlpha(1.f));
				sg.strokePath (J, PathStrokeType(mLineThickness*0.75f));
                
			}
        }
	}
	
	if(mThumbLayerNeedsRedraw)
	{
		Graphics tg(mThumbImage);	
		mThumbImage.clear(Rectangle<int>(0, 0, compWidth, compHeight), Colours::transparentBlack);
		
		// draw thumbs
		if (doDial1 && mDrawThumb)
		{
			myLookAndFeel->drawMLButtonShape (tg, thumb1, cornerSize, 
				thumb_normal, outline, mLineThickness*0.75f, thumbAdorn1, tip1.x(), tip1.y()); 							
		}
		if (doDial2 && mDrawThumb)
		{
			myLookAndFeel->drawMLButtonShape (tg, thumb2, cornerSize,
				thumb_normal, outline, mLineThickness*0.75f, thumbAdorn2, tip2.x(), tip2.y()); 		
		}
	}
    
	// composite images
	//
	if(mParameterImage.isValid())
	{
		g.drawImage (mParameterImage, 0, 0, compWidth, compHeight, 0, 0, compWidth, compHeight, false); 
	}				
	
	if(mStaticImage.isValid())
	{
		g.drawImage (mStaticImage, 0, 0, compWidth, compHeight, 0, 0, compWidth, compHeight, false); 
	}	
	// TODO composite and move smaller thumb image
	if(mThumbImage.isValid())
	{
		g.drawImage (mThumbImage, 0, 0, compWidth, compHeight, 0, 0, compWidth, compHeight, false); 
	}
	
	// finally, draw numbers over composited images
	//	
	if(1)//(mParameterLayerNeedsRedraw)
	{
		if (doDial1 && mDrawThumb)
		{
			if (myLookAndFeel->mDrawNumbers && mDoNumber)
			{
				g.setOpacity(isEnabled() ? (glow1 ? 0.8 : 1.) : 0.4f);
				const char* numBuf = myLookAndFeel->formatNumber(val1, mDigits, mPrecision, mDoSign, mValueDisplayMode);				
				myLookAndFeel->drawNumber(g, numBuf, text1.x(), text1.y(), text1.getWidth(), 
					text1.getHeight(), textColor, Justification::centred); 
			}
		}
		if (doDial2 && mDrawThumb)
		{
			if (myLookAndFeel->mDrawNumbers && mDoNumber)
			{
				g.setOpacity(isEnabled() ? (glow2 ? 0.8 : 1.) : 0.4f);
				const char* numBuf = myLookAndFeel->formatNumber(val2, mDigits, mPrecision, mDoSign, mValueDisplayMode);
				myLookAndFeel->drawNumber(g, numBuf, text2.x(), text2.y(), text2.getWidth(), 
					text2.getHeight(), textColor, Justification::centred); 
			}
		}
	}

	mParameterLayerNeedsRedraw = mStaticLayerNeedsRedraw = mThumbLayerNeedsRedraw = false;
}

#pragma mark rotary dial

void MLDial::drawRotaryDial (Graphics& g, int rx, int ry, int rw, int rh, float dialPos)
{	
//	int width = trackRect.getWidth();
//	int height = trackRect.getHeight();

 	MLLookAndFeel* myLookAndFeel = MLLookAndFeel::getInstance();
	const MLRect uBounds = getGridBounds();
	const MLRect boundsRect = getWidgetLocalBounds();

	// Colors TODO sort out, calculate less often
	const Colour trackDark = (mTrackDarkColor.withMultipliedAlpha (isEnabled() ? 1.f : 0.5f));					
	const Colour shadow (findColour(MLLookAndFeel::shadowColor).withAlpha (isEnabled() ? 1.f : 0.5f));
	const Colour outline_color (findColour(MLLookAndFeel::outlineColor).withAlpha (isEnabled() ? 1.f : 0.5f));
	const Colour fill_color (mTrackFillColor.withAlpha (isEnabled() ? 1.0f : 0.5f));
	const Colour thumb_color = mFillColor.withAlpha (isEnabled() ? 1.f : 0.5f);
	const Colour glow_color = mGlowColor.withAlpha (isEnabled() ? 1.f : 0.5f);
	const Colour indicator_color (mIndicatorColor.withAlpha (isEnabled() ? 0.75f : 0.35f));
	const Colour signal_color (mIndicatorColor.withAlpha (isEnabled() ? 1.f : 0.f));
	const Colour glow1Color = (trackDark.overlaidWith(indicator_color.withAlpha(0.6f))).withMultipliedAlpha(0.25f);
	const Colour glow3Color = outline_color.withAlpha(0.125f);

	// Dimensions
	const float r1 = mDiameter*0.5f;
	const MLPoint center = getDialCenter();
	float cx, cy;
	cx = (int)center.x() + 0.5;
	cy = (int)center.y();
	
    const float iy = cy;	// indicator y
    const float rx1 = cx - r1;
    const float ry1 = cy - r1;
	
	float indicator_thick = mLineThickness;
	
	bool do_shadow = true;
	bool do_indicator = true;
	bool do_ticks = true;
	if (uBounds.height() <= 0.5f) 
	{
		do_shadow = 0;   // ?
		indicator_thick = 0.5;
		do_ticks = 0;
	}
	
	float posA, posB;
	float angleA, angleB, angleI, angleM;
	float middlePos = valueToProportionOfLength(0.);
	
	if (mBipolar)
	{
		if (dialPos < middlePos)
		{
			posA = dialPos;
			posB = middlePos;
		}
		else
		{
			posA = middlePos;
			posB = dialPos;
		}
	}
	else
	{
		posA = 0.;
		posB = dialPos;
	}
	
	angleA = rotaryStart + posA * (rotaryEnd - rotaryStart);
	angleB = rotaryStart + posB * (rotaryEnd - rotaryStart);
	angleI = rotaryStart + dialPos * (rotaryEnd - rotaryStart);
	angleM = rotaryStart + middlePos * (rotaryEnd - rotaryStart);
		
	float ttop = 0., tleft = 0.;
	ttop = cy + mMargin*uBounds.height()*0.5f;
	tleft = cx;

 	// parameter layer
	if (mParameterLayerNeedsRedraw)
	{	
		Graphics pg(mParameterImage);	
		mParameterImage.clear(MLToJuceRectInt(boundsRect), Colours::transparentBlack);
		
		// unfilled area(s)
		{	
            Path track;
			pg.setColour (trackDark);
            track.addPieSegment (rx1, ry1, mDiameter, mDiameter, rotaryStart, angleA, 0.);
            track.addPieSegment (rx1, ry1, mDiameter, mDiameter, angleB, rotaryEnd, 0.);
            pg.fillPath (track);
		}
		
		// big fill area
		{	
            Path filledArc;
			pg.setColour (fill_color);
            filledArc.addPieSegment (rx1, ry1 , mDiameter, mDiameter, angleA, angleB, 0.);
            pg.fillPath (filledArc);
		}

		bool glow = false;
		if (glow)
		{
			Path track;
            track.addPieSegment (rx1, ry1, mDiameter, mDiameter, rotaryStart, rotaryEnd, 0.);
			ColourGradient cg (glow1Color, rx1 + (mDiameter/2.), ry1 + (mDiameter/2.), glow3Color, rx1, ry1, true); // radial
			pg.setGradientFill(cg);
			pg.fillPath (track);
		}
		
		// indicator line
		if (do_indicator)
		{	
			Path indicator;
			indicator.startNewSubPath(0., 0.);
			indicator.lineTo(0., -r1+0.5);
			pg.setColour (indicator_color);
            pg.strokePath (indicator, PathStrokeType(indicator_thick), AffineTransform::rotation(angleI).translated (cx, iy));
		}
		
		// hilight ( for patcher connections to tiny dials)
		if (mHilighted)
		{
			const float m = mLineThickness*3.;
			const float mh = m / 2.;
            Path track;
			pg.setColour (mHilightColor);
            track.addArc(rx1-mh, ry1-mh, mDiameter+m, mDiameter+m, 0.f, kMLTwoPi, true);
            pg.strokePath (track, PathStrokeType(m));			
		}
	}
    
	// static layer
	if (mStaticLayerNeedsRedraw)
    {
		const Colour label_color = (findColour(MLLookAndFeel::labelColor).withAlpha (isEnabled() ? 1.f : 0.5f));	
		mStaticImage.clear(MLToJuceRectInt(boundsRect), Colours::transparentBlack);

		Graphics sg(mStaticImage);	
		{	
			// outer shadow
			Path outline;
			float d, opacity;
			for (int i=0; i<mShadowSize; i++)
			{
				outline.clear();			
				outline.addCentredArc(cx, cy, r1 + i + 0.5, r1 + i + 0.5, 0., rotaryStart, rotaryEnd, true);
				d = (float)(mShadowSize - i) / (float)mShadowSize; // 0. - 1.
				opacity = d * d * d * kMLShadowOpacity;
				sg.setColour (shadow.withAlpha(opacity));
				sg.strokePath (outline, PathStrokeType (1.f));	
			}
			
		}		
		
		{	
			// track outline
			Path outline;
			outline.addCentredArc(cx, cy, r1, r1, 0., rotaryStart, rotaryEnd, true);			
			sg.setColour (outline_color.withMultipliedAlpha(0.15f));
			sg.strokePath (outline, PathStrokeType (2.f*mLineThickness));	
			sg.setColour (outline_color);
			sg.strokePath (outline, PathStrokeType (0.75f*mLineThickness));
		}

		if (do_ticks)
		{	
			float angle;
			Path tick;
			tick.startNewSubPath(0, -r1);
			tick.lineTo(0, -r1-mTickSize);
			sg.setColour (outline_color);
			for (int t=0; t<mTicks; t++)
			{
				angle = rotaryStart + (t * (rotaryEnd - rotaryStart) / (mTicks - 1)) ;
				angle += mTicksOffsetAngle;
				sg.strokePath (tick, PathStrokeType(mLineThickness), AffineTransform::rotation (angle).translated (cx, cy - 0.5f));
			}
		}

		// draw detents
		if(mDetents.size() > 0)
		{
			float x1, y1, x2, y2;		
			for (unsigned i=0; i<mDetents.size(); ++i)
			{
				float td = valueToProportionOfLength(mDetents[i].mValue); 
				
				// if the detent has a label, it's a line under the text, otherwise a small dot.
				float theta = rotaryStart + (td * (rotaryEnd - rotaryStart));
	
				bool coveringEnd = approxEqual(theta, rotaryStart) || approxEqual(theta, rotaryEnd);
				if (!coveringEnd)
				{
					Path J;
					float thickness = mLineThickness;

					// draw detent - outer edge lines up with tick
					AffineTransform t1 = AffineTransform::rotation(theta).translated(cx, cy);
					x1 = 0;
					x2 = 0;
					float tW = 0.875f; //  tweak
					y1 = -r1 - (mTickSize*tW);
					y2 = -r1 - (mTickSize*tW)*(1.f - mDetents[i].mWidth);
					t1.transformPoint(x1, y1);
					t1.transformPoint(x2, y2);					
					J.startNewSubPath(x1, y1);
					J.lineTo(x2, y2);
					sg.setColour (label_color);
					sg.strokePath (J, PathStrokeType(thickness));
				}
			}
		}		
	}	
	
	// composite images
	if(mParameterImage.isValid())
	{
		g.drawImage (mParameterImage, rx, ry, rw, rh, rx, ry, rw, rh, false); 
	}
		
	// draw number text over composited images
	if (1)//(mParameterLayerNeedsRedraw)
	{
		if (myLookAndFeel->mDrawNumbers && mDoNumber)
		{
			// draw background under text
			if (!isOpaque())
				myLookAndFeel->drawBackgroundRect(g, this, mRotaryTextRect);
            
			float textSize = mTextSize;
			float op = isEnabled() ? 1.f : 0.4f;
			const char* numBuf = myLookAndFeel->formatNumber(getValue(), mDigits, mPrecision, mDoSign, mValueDisplayMode);
			myLookAndFeel->drawNumber(g, numBuf, tleft, ttop, boundsRect.getWidth() - tleft, textSize,
                                      findColour(MLLookAndFeel::outlineColor).withAlpha(op));
		}
	}
    
 	if(mStaticImage.isValid())
	{
		g.drawImage (mStaticImage,rx, ry, rw, rh, rx, ry, rw, rh, false);
	}
 	
	mParameterLayerNeedsRedraw = mStaticLayerNeedsRedraw = false;
}

#pragma mark -
#pragma mark resize

void MLDial::moved()
{
	mParameterLayerNeedsRedraw = mStaticLayerNeedsRedraw = true;
}

void MLDial::resized()
{
}

void MLDial::sizeChanged()
{
	resized();
}

void MLDial::visibilityChanged()
{
	mParameterLayerNeedsRedraw = mStaticLayerNeedsRedraw = true;
}

void MLDial::mouseDown (const MouseEvent& e)
{
    mouseWasHidden = false;
	mParameterLayerNeedsRedraw = true;
	mThumbLayerNeedsRedraw = true;
    if (isEnabled())
    {		
		sendDragStart();
		findDialToDrag(e);	// sets dialToDrag
		dialBeingDragged = dialToDrag;

		if (dialBeingDragged != NoDial)
		{
			mLastDragX = e.x;
			mLastDragY = e.y;
			mLastDragTime = Time(e.eventTime.toMilliseconds());
			mFilteredMouseSpeed = 0.;
			mMouseMotionAccum = 0;
			
			if (dialBeingDragged == MaxDial)
				valueWhenLastDragged = valueMax;
			else if (dialBeingDragged == MinDial)
				valueWhenLastDragged = valueMin;
			else
				valueWhenLastDragged = currentValue;

			valueOnMouseDown = valueWhenLastDragged;
			mouseDrag(e);
		}
    }
}

void MLDial::mouseUp (const MouseEvent&)
{
    if (isEnabled())
    {
		sendDragEnd();
		if (dialBeingDragged != NoDial)
		{
			restoreMouseIfHidden();
			if (sendChangeOnlyOnRelease && valueOnMouseDown != currentValue)
				triggerChangeMessage (false);
		}
    }
	endDrag();
}

void MLDial::restoreMouseIfHidden()
{
    if (mouseWasHidden)
    {
        mouseWasHidden = false;
        for (int i = Desktop::getInstance().getNumMouseSources(); --i >= 0;)
            Desktop::getInstance().getMouseSource(i)->enableUnboundedMouseMovement (false);

        Point<int> mousePos;
		mousePos = Desktop::getLastMouseDownPosition();
        Desktop::setMousePosition (mousePos);
    }
}

void MLDial::hideMouse()
{
	for (int i = Desktop::getInstance().getNumMouseSources(); --i >= 0;)
		Desktop::getInstance().getMouseSource(i)->hideCursor(); 
}

void MLDial::modifierKeysChanged (const ModifierKeys& modifiers)
{
    if (isEnabled()
         && style != MLDial::Rotary
         && isVelocityBased == modifiers.isAnyModifierKeyDown())
    {
        restoreMouseIfHidden();
    }
}

void MLDial::mouseDoubleClick (const MouseEvent&)
{
    if (doubleClickToValue
         && isEnabled()
         && minimum <= doubleClickReturnValue
         && maximum >= doubleClickReturnValue)
    {
        sendDragStart();
        setSelectedValue (doubleClickReturnValue, mainValue, true, true);
		sendDragEnd();
    }
}

float MLDial::getNextValue(float oldVal, int dp, bool doFineAdjust, int stepSize)
{		
	float val = oldVal;
	float r = val;
	int detents = mDetents.size();
	if ((val < mZeroThreshold) && (dp > 0))
	{
		val = mZeroThreshold;
	}
	if(detents && mSnapToDetents && !doFineAdjust)
	{	
		// get next detent, collecting mouse motion if needed
		bool doStep = true;
		int direction = sign(dp);
		if (stepSize > 1)
		{
			mMouseMotionAccum += direction;
			if (abs(mMouseMotionAccum) > stepSize)
			{
				mMouseMotionAccum = 0;
			}
			else 
			{
				doStep = false;
			}
		}
		
		if (doStep)
		{
			mCurrentDetent = nearestDetent(val);				
			mCurrentDetent += direction;
			mCurrentDetent = clamp(mCurrentDetent, 0, detents - 1);
			r = mDetents[mCurrentDetent].mValue;
		}
	}
	else 
	{
		MLLookAndFeel* myLookAndFeel = MLLookAndFeel::getInstance();
		int d = myLookAndFeel->getDigitsAfterDecimal(val, mDigits, mPrecision);
		d = clamp(d, 0, 3);
		float minValChange = max(powf(10., -d), interval);		
		if(doFineAdjust)
		{
			// get minimum visible change as change in position
			float p2 = valueToProportionOfLength (val + minValChange * dp);
			r = proportionOfLengthToValue (clamp (p2, 0.0f, 1.0f));			
		}
		else 
		{
			// move dial 1/100 of length range * drag distance
			float p1 = valueToProportionOfLength (val);
			float p2 = p1 + dp*0.01f;			
			r = proportionOfLengthToValue (clamp (p2, 0.0f, 1.0f));
			
			// if this motion is too small to change the displayed value, 
			// do the smallest visible change instead
			if(dp > 0)
			{
				if (r < val + minValChange)
				{
					r = val + minValChange;
				}
			}
			else if(dp < 0)
			{
				if (r > val - minValChange)
				{
					r = val - minValChange;
				}
			}
		}
	}
	return r;
}

void MLDial::mouseDrag (const MouseEvent& e)
{
	const MLRect uBounds = getGridBounds();
	const bool doInitialJump = uBounds.height() > 0.5f;

	int detents = mDetents.size();
	
	// TODO can we make shift work if pressed during drag?
	// right now it depends on the host. 
	bool doFineAdjust = e.mods.isShiftDown();

	findDialToDrag(e);
			
    if (isEnabled() && (! menuShown))
    {
		// command click returns to doubleClickValue and exits.
		if ((e.mods.isCommandDown()) && doubleClickToValue)
		{
			if ( isEnabled()
				 && minimum <= doubleClickReturnValue
				 && maximum >= doubleClickReturnValue)
			{
				sendDragStart();
				setSelectedValue (doubleClickReturnValue, mainValue, true, true);
				sendDragEnd();
			}
			return;
		}
		
		// snap to position on first click.
		// first click for Rotary style snaps to position.
		if (doInitialJump && e.mouseWasClicked())
		{
			if (style == MLDial::Rotary)
			{
				int width = trackRect.getWidth();
	//			int height = trackRect.getHeight();
		
				if(width > kMinimumDialSizeForJump)
				{		
					const MLPoint center = getDialCenter();
					int dx = e.x - center.x();
					int dy = e.y - center.y();

					// if far enough from center
					if (dx * dx + dy * dy > 9)
					{
						float angle = atan2 ((float) dx, (float) -dy);
						while (angle < rotaryStart)
							angle += double_Pi * 2.0;

						// if between start and end
						if (angle < rotaryEnd)
						{
							const float proportion = (angle - rotaryStart) / (rotaryEnd - rotaryStart);
							valueWhenLastDragged = proportionOfLengthToValue (clamp (proportion, 0.0f, 1.0f));
						}
					}
				}
			}
			else // linear
			{
				float start = isHorizontal() ? trackRect.left() : trackRect.top();
				float extent = isHorizontal() ? trackRect.width() : trackRect.height();
				if (mOverTrack)
				{
					const int mousePos = (isHorizontal() || style == MLDial::RotaryHorizontalDrag) ? e.x : e.y;
					float scaledMousePos = (mousePos - start) / extent;

					if (isVertical())
					{
						scaledMousePos = 1.0f - scaledMousePos;
					}
					valueWhenLastDragged = proportionOfLengthToValue (clamp (scaledMousePos, 0.0f, 1.0f));
				}		
			}
			if(detents && mSnapToDetents && !doFineAdjust)
			{				
				mCurrentDetent = nearestDetent(valueWhenLastDragged);
				valueWhenLastDragged = mDetents[mCurrentDetent].mValue;
			}
		}
		
		else if (dialBeingDragged != NoDial) 
		{
			e.source.hideCursor();
			e.source.enableUnboundedMouseMovement (true, false);
			mouseWasHidden = true;

			int dp = isHorizontal() ? (e.x - mLastDragX) : -(e.y - mLastDragY);
			mLastDragX = e.x;
			mLastDragY = e.y;
			float val = getValueOfDial(dialBeingDragged);
			if(dp != 0)
			{
				valueWhenLastDragged = getNextValue(val, dp, doFineAdjust, kDragStepSize);	
			}		
		}		
		setValueOfDial(dialBeingDragged, valueWhenLastDragged);
    }
}

void MLDial::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
	// filter out zero motions from trackpad
	if ((wheel.deltaX == 0.) && (wheel.deltaY == 0.)) return;

	bool doFineAdjust = e.mods.isShiftDown();
	findDialToDrag(e);

    if (scrollWheelEnabled && isEnabled())
    {
        if (maximum > minimum && ! isMouseButtonDownAnywhere())
        {
			float dpf;
			if (isHorizontal())
			{
				dpf = (wheel.deltaX != 0 ? -wheel.deltaX : wheel.deltaY);
			}
			else 
			{
				dpf = (wheel.deltaY);
			}
            
            if(wheel.isReversed) dpf = -dpf;			
			float val = getValueOfDial(dialToDrag);
			int dir = sign(dpf);
			int dp = dir*max(1, (int)(fabs(dpf)*32.)); // mouse scale for no detents
			valueWhenLastDragged = getNextValue(val, dp, doFineAdjust, kMouseWheelStepSize);			
            sendDragStart();
			setValueOfDial(dialToDrag, valueWhenLastDragged);
			sendDragEnd();
        }
    }
    else
    {
        Component::mouseWheelMove (e, wheel);
    }
}

#pragma mark -
#pragma mark ML Stuff

const MLPoint MLDial::getDialCenter() const
{
	return MLPoint(mDialCenter.x(), mDialCenter.y());
}

void MLDial::setTicks (int t)
{
	mTicks = t;
}

void MLDial::setTicksOffsetAngle (float t)
{
	mTicksOffsetAngle = t;
}

// the colors for different MLDial parts are generated algorithmically.
void MLDial::setFillColor (const Colour& color)
{
	// fill for thumb
    mFillColor = color;
	
	// fill for full track	
    mTrackFillColor = color.overlaidWith(Colours::black.withAlpha(0.05f));

	// indicator line
	mIndicatorColor = brightLineColor(color);
	
	// glow (background for rollover) 
	mGlowColor = mFillColor.overlaidWith(mIndicatorColor.withAlpha(0.10f));
	mThumbGlowColor = mFillColor.overlaidWith(mIndicatorColor.withAlpha(0.75f));
	
	mTrackDarkColor = findColour(MLLookAndFeel::darkFillColor);	

	//lookAndFeelChanged();
}

void MLDial::setHilight (bool h)
{
	if (h != mHilighted)
	{
		mHilighted = h;
		mParameterLayerNeedsRedraw = true;
		repaint();
	}
}

void MLDial::setHilightColor (const Colour& color)
{
	mHilightColor = color;
}

MLDial::WhichDial MLDial::getRectOverPoint(const MouseEvent& e)
{
	int x = e.getMouseDownX();
	int y = e.getMouseDownY();

	return getRectOverPoint(x, y);
}

MLDial::WhichDial MLDial::getRectOverPoint(const int xx, const int yy)
{
	WhichDial which = NoDial;
	
	// looks correct for all our Juce lines centered on 0.5
	const int x = xx - 1;
	const int y = yy - 1;

	MLRect minRect, maxRect, mainRect, fieldRect;
	float minPos, maxPos, dialPos;
	dialPos = getLinearDialPos (currentValue);
    minPos = getLinearDialPos (valueMin);
    maxPos = getLinearDialPos (valueMax);

	if (isTwoOrThreeValued())
	{
		// can cache these.
		getDialRect (minRect, Thumb1Rect, dialPos, minPos, maxPos);
		getDialRect (maxRect, Thumb2Rect, dialPos, minPos, maxPos);
	
		if (minRect.contains(x, y))
		{
//	printf("clicked multiMIN\n");
			which = MinDial;
		}
		else if (maxRect.contains(x, y))
		{
//	printf("clicked multiMAX\n");
			which = MaxDial;
		}
		else if (trackRect.contains(x, y))
		{
//	printf("clicked multiTRACK\n");
			which = TrackDial;
		}
	}
	else
	{
		getDialRect (trackRect, TrackRect, dialPos, minPos, maxPos);
		getDialRect (mainRect, mTopLeft ? Thumb1Rect : Thumb2Rect, dialPos, minPos, maxPos);
		getDialRect (fieldRect, mTopLeft ? Thumb1Field : Thumb2Field, dialPos, minPos, maxPos);
		
		if (mainRect.contains(x, y))
		{
//	printf("clicked MAIN\n");
			which = MainDial;
		}
		else if (trackRect.contains(x, y))
		{
//	printf("clicked TRACK\n");
			which = TrackDial;
		}
		else if (fieldRect.contains(x, y))
		{
//	printf("clicked FIELD\n");
			which = MainDial;
		}
	}
	
	return which;
}

// findDialToDrag: usually same as thumb area over point.  But if
// we are in track, find closest thumb to mouse position.
// SIDE EFFECT: sets mInTrack.

void MLDial::findDialToDrag(const MouseEvent& e)
{
	int x = e.getMouseDownX();
	int y = e.getMouseDownY();
	findDialToDrag(x, y);
}

void MLDial::findDialToDrag(const int x, const int y)
{
	float min, max;
	WhichDial thumb;
// printf ("[%f], min %f, max %f, current %f\n", mousePos, min, max, current);
	
	if (getDialStyle() == MLDial::Rotary)
	{
		thumb = MainDial;
	}
	else
	{
		// if over area covered by a MLDial, drag that one. 
		thumb = getRectOverPoint(x, y);
		
		if (thumb == TrackDial)
		{
			mOverTrack = true;
			if (isTwoOrThreeValued())
			{		
				float tweak = isVertical() ? 0.1 : -0.1;  // for min/max order when equal
				min = getPositionOfValue (getMinValue());
				max = getPositionOfValue (getMaxValue());
				
				const float mousePos = (float) (isVertical() ? y : x);
	//			printf("in track: min %f, max %f, mouse %f\n", min, max, mousePos);
				const float minPosDistance = fabsf (min + tweak - mousePos);
				const float maxPosDistance = fabsf (max - tweak - mousePos);
			
				if (maxPosDistance <= minPosDistance)
				{
	//			printf ("drag max\n");
					thumb = MaxDial;
				}
				else
				{
	//			printf ("drag min\n");
					thumb = MinDial;
				}
			}
			else
			{
				thumb = MainDial;
			}
		}
		else
		{
			mOverTrack = false;
		}
	}
	
	dialToDrag = thumb;
}

void MLDial::addDetent(const float value, const float width)
{
    if(value > mZeroThreshold)
    {
        mDetents.push_back(MLDialDetent(value, width));
    }
    else
    {
        MLError() << "MLDial::addDetent: value below zero threshold!\n";
    }
}

void MLDial::snapToDetents(const bool snap)
{
	mSnapToDetents = snap;
}

#pragma mark dial dims

// return whole area for specified area of the dial in bounds sr,
// given positions of dials. 
//
void MLDial::getDialRect (MLRect& ret,
	const MLDial::DialRect whichRect,
	const float dialPos, const float minDialPos, const float maxDialPos) 
{
 	MLLookAndFeel* myLookAndFeel = MLLookAndFeel::getInstance();
	bool num = myLookAndFeel->mDrawNumbers && mDoNumber;
    bool smallThumbs = getAttribute("small_thumbs");
	
	// get max width for either number. 
	int thumbDefaultWidth = ((int)(mTextHeight)/* & ~0x1*/) + 2 ;
	
	bool multi = (isTwoOrThreeValued());
		
	MLRect full, notFull1, notFull2;

	float middlePos = getLinearDialPos(0.);
	float iPos1, iPos2; 
	float fillPos1, fillPos2; 
	float val1, val2;
	bool doDial1, doDial2;
	if (multi)
	{
		doDial1 = doDial2 = true;
		val1 = getMinValue();
		val2 = getMaxValue();
		fillPos1 = iPos1 = (minDialPos);		
		fillPos2 = iPos2 = (maxDialPos);		
	}
	else
	{
		doDial1 = mTopLeft;
		doDial2 = !doDial1;
		val1 = val2 = getValue();
		iPos1 = iPos2 = floor(dialPos);		
		if (mBipolar)
		{
			if (dialPos < middlePos)
			{
				fillPos1 = (dialPos);
				fillPos2 = (middlePos);
			}
			else
			{
				fillPos1 = (middlePos);
				fillPos2 = (dialPos);
			}
		}
		else
		{
			fillPos1 = fillPos2 = (dialPos);		
		}
	}
	
	// get sizes of currently displayed numbers
    int numWidth1, numWidth2;
    if(!smallThumbs)
    {
        numWidth1 = mTextSize*myLookAndFeel->getNumberWidth(val1, mDigits, mPrecision, mDoSign);
        numWidth2 = mTextSize*myLookAndFeel->getNumberWidth(val2, mDigits, mPrecision, mDoSign);
        numWidth1 |= 0x1;
        numWidth2 |= 0x1;
    }
    else
    {
        numWidth1 = 1;
        numWidth2 = 1;
    }
	
 	MLRect text1Size(0, 0, numWidth1, mTextHeight);
	MLRect text2Size(0, 0, numWidth2, mTextHeight);

	// get thumb size
    int thumbWidth, thumbHeight;
	if (isHorizontal())
	{
        if(smallThumbs)
        {
            thumbWidth = thumbDefaultWidth;
            thumbHeight = mMargin*2;
        }
        else
        {
            thumbWidth = (num ? mMaxNumberWidth : thumbDefaultWidth) + mThumbMargin*2;
            thumbHeight = mTextHeight + mThumbMargin*2 ;
        }
    }
    else
    {
        if(smallThumbs)
        {
            thumbWidth = mMargin*2;
            thumbHeight = thumbDefaultWidth;
        }
        else
        {
            thumbWidth = (num ? mMaxNumberWidth : thumbDefaultWidth) + mThumbMargin*2;
            thumbHeight = mTextHeight + mThumbMargin*2 ;
        }
    }
	
	MLRect thumbSize(0, 0, thumbWidth, thumbHeight);

	// get thumb center(s)
	Vec2 thumb1Center, thumb2Center;
	if (isHorizontal())
	{
		thumb1Center = Vec2(iPos1 + 1, trackRect.top() - thumbHeight/2 - 1);
		thumb2Center = Vec2(iPos2 + 1, trackRect.bottom() + thumbHeight/2 + 1);
	}
	else
	{
		thumb1Center = Vec2(trackRect.left() - thumbWidth/2, iPos1 + 1);
		thumb2Center = Vec2(trackRect.right() + thumbWidth/2, iPos2 + 1);
	}
	
	// get adornment
	int thumbAdorn1, thumbAdorn2;
	if (isHorizontal())
	{
		thumbAdorn1 = eMLAdornBottomLeft | eMLAdornBottom | eMLAdornBottomRight;
		thumbAdorn2 = eMLAdornTopLeft | eMLAdornTop | eMLAdornTopRight;
	}
	else
	{
		thumbAdorn1 = eMLAdornTopRight | eMLAdornRight | eMLAdornBottomRight;	
		thumbAdorn2 = eMLAdornTopLeft | eMLAdornLeft | eMLAdornBottomLeft;
	}

	// get tip
	Vec2 thumb1Tip = thumb1Center;
	Vec2 thumb2Tip = thumb2Center;
	if (isHorizontal())
	{
		thumb1Tip += Vec2(0, mTrackThickness + thumbHeight/2);
		thumb2Tip -= Vec2(0, mTrackThickness + thumbHeight/2);
        if(smallThumbs)
        {
            thumb1Tip += Vec2(1, 0);
            thumb2Tip -= Vec2(1, 0);
        }
	}
	else
	{
		thumb1Tip += Vec2(mTrackThickness + thumbWidth/2, 0);
		thumb2Tip -= Vec2(mTrackThickness + thumbWidth/2, 0);
	}
	
	// get fill rects
	full = trackRect;
	notFull1 = trackRect;
	notFull2 = trackRect;
	if (isHorizontal())
	{
		int l = trackRect.left();
		int r = trackRect.right();
		if (multi || mBipolar)
		{		
			notFull1.setWidth(fillPos1 - l);
			full.setLeft(fillPos1);
			full.setWidth(fillPos2 - fillPos1);
			notFull2.setLeft(fillPos2);
			notFull2.setWidth(r - fillPos2);
		}
		else
		{	
			notFull2.setBounds(0, 0, 0, 0);
			int fp = doDial1 ? fillPos1 : fillPos2;
			full.setWidth(fp - l);
			notFull1.setLeft(fp);
			notFull1.setWidth(r - fp);
		}
	} 
	else
	{
		int t = trackRect.top();
		int b = trackRect.bottom();
		if (multi || mBipolar)
		{		
			notFull1.setTop(t); 
			notFull1.setHeight(t - fillPos2); 
			full.setTop(fillPos2);
			full.setHeight(fillPos1 - fillPos2);
			notFull2.setTop(fillPos1); 
			notFull2.setHeight(b - fillPos1);
		}
		else
		{	
			notFull2.setBounds(0, 0, 0, 0);
			int fp = doDial1 ? fillPos1 : fillPos2;
			notFull1.setTop(t); 
			notFull1.setHeight(fp - t); 
			full.setTop(fp);
			full.setHeight(b - fp);
		}
	}

	switch (whichRect)
	{
		case MLDial::TrackRect:
			ret = trackRect;
			break;
		case MLDial::Thumb1Rect:		
			ret = thumbSize.withCenter(thumb1Center);
			break;
		case MLDial::Thumb2Rect:   
			ret = thumbSize.withCenter(thumb2Center);
			break;			
		case MLDial::Thumb1Field:		
			ret = trackRect;
			ret.setToUnionWith(thumbSize.withCenter(thumb1Center));
			break;
		case MLDial::Thumb2Field:   
			ret = trackRect;
			ret.setToUnionWith(thumbSize.withCenter(thumb2Center));
			break;
		case MLDial::Text1Rect:
			ret = text1Size.withCenter(thumb1Center);
			break;
		case MLDial::Text2Rect:
			ret = text2Size.withCenter(thumb2Center);
			break;
		case MLDial::Tip1Rect:
			ret = thumb1Tip;
			break;
		case MLDial::Tip2Rect:
			ret = thumb2Tip;
			break;
		case MLDial::FillRect:
			ret = full;
			break;
		case MLDial::NoFillRect1:
			ret = notFull1;
			break;
		case MLDial::NoFillRect2:
			ret = notFull2;
			break;
	}
	
	correctRect(ret);
}

// resize this Dial and set the track rect, from which all the other 
// parts are calculated
void MLDial::resizeWidget(const MLRect& b, const int u)
{
	Component* pC = getComponent();
	if(pC)
	{
 		MLWidget::resizeWidget(b, u);
     
		MLLookAndFeel* myLookAndFeel = MLLookAndFeel::getInstance();
		const MLRect uBounds = getGridBounds();
		bool multi = (isTwoOrThreeValued());
        bool smallThumbs = getAttribute("small_thumbs");
		
		// adapt vrect to juce rect
		MLRect bb = b;
		float top =  bb.top();
		float width =  bb.width();
		float height = bb.height();	
		
		Vec2 bCenter = b.getCenter();		
		mMargin = myLookAndFeel->getSmallMargin() * u;
		mTrackThickness = (int)((float)kMLTrackThickness * u / 64.);
		mTrackThickness |= 1; // make odd
		mLineThickness = u/48.f;
		mTextSize = (float)u*myLookAndFeel->getDialTextSize(*this);
		if (mDoNumber)
		{
			mMaxNumberWidth = myLookAndFeel->calcMaxNumberWidth(mDigits, mPrecision, mDoSign)*mTextSize + 2.;
//			debug() << "dial " << getWidgetName() << " digits:" << mDigits << " precis:" << 
//				mPrecision << " width:" << mMaxNumberWidth << "\n";
		}
		else
		{
			mMaxNumberWidth = 0;
		}
		
		// get component bounds
		//
		Rectangle<int> cBounds;		
		if (style == MLDial::Rotary)
		{
			float minDim = min(width, height);
			
			// get diameter.
			// TODO make this a clearer customization or different class
			// for small knobs with no numbers.
			// 
			int cx, cy;

			if (uBounds.height() <= 0.5f) 
			{
				mShadowSize = (int)(kMLShadowThickness*u/48.); // for small dials
				mDiameter = minDim - mShadowSize*2.;
				mTickSize = 0.;
			}
			else
			{
				mShadowSize = (int)(kMLShadowThickness*u/32.);
				mDiameter = minDim * 0.75f;
				mTickSize = mDiameter * 0.1f;
			}
			
			cx = mDiameter/2 + max(mShadowSize, (int)mTickSize); 
			cy = cx;
			mDialCenter = Vec2(cx, cy);

			if (uBounds.height() <= 0.75f) 
			{
				height = cy*2;
			}
			else
			{
				// cut off bottom
				height = (float)cy*1.8f ;
			}
						
			int newLeft = bCenter.x() - cx;

			// resize input rect, keeping center constant				
			width = cx*2 + 1; 
			
			// if there is not enough room for number, expand right side 
			int rightHalf = width - cx;
			if (mMaxNumberWidth > rightHalf)
			{
				int newWidth = cx + mMaxNumberWidth;
				width = newWidth;
			}			
			
			cBounds = Rectangle<int>(newLeft, top, (int)width, (int)height);		
			
			MLRect tr(newLeft, top, (int)width, (int)height);
			trackRect = tr;

			mRotaryTextRect = MLRect(cx, cy, mMaxNumberWidth, height - cy);
		}
		else if(smallThumbs)
        // linear with fixed track size, small thumbs, border expands 
        {
            MLRect bb = b;
			mMaxNumberWidth = ((int)(mMaxNumberWidth) & ~0x1) + 2 ;			
			mShadowSize = (int)(kMLShadowThickness*u/32.) & ~0x1;
			mTextHeight = (((long)mTextSize) | 0x1) - 2;
			mThumbMargin = (int)(myLookAndFeel->getSmallMargin()*u*0.75f);
            int smallThumbSize = (int)(u*0.5f);

			// get track size
			if (isHorizontal())
			{
                trackRect = MLRect(0, 0, bb.width(), mTrackThickness);
                trackRect.stretchWidth(-2);
			}
			else
			{
				trackRect = MLRect(0, 0, mTrackThickness, bb.height());
                trackRect.stretchHeight(-2);
			}
            
            // expand boundary to fit thumbs
            if (isHorizontal())
            {
                bb.expand(smallThumbSize);
            }
            else
            {
                bb.expand(smallThumbSize);
            }
			cBounds = MLToJuceRectInt(bb);
            
			// get track position relative to bounds rect
			{
				trackRect.setCenter(bb.getSize());
			}
			
        }
        else // normal linear, track shrinks to fit
		{
            MLRect bb = b;
			// max widths for dials are too long for horiz thumbs - correct
			mMaxNumberWidth = ((int)(mMaxNumberWidth) & ~0x1) + 2 ;			
			cBounds = MLToJuceRectInt(bb);
			
			mShadowSize = (int)(kMLShadowThickness*u/32.) & ~0x1;			
			mTextHeight = (((long)mTextSize) | 0x1) - 2;
			mThumbMargin = (int)(myLookAndFeel->getSmallMargin()*u*0.75f);
			int padding = mShadowSize + mTrackThickness/2;
			int thumbHeight = mTextHeight + mThumbMargin*2 ;
			Vec2 maxThumbSize(thumbHeight*3, thumbHeight + padding);

			// get track size
			if (isHorizontal())
			{
                trackRect = MLRect(0, 0, bb.width(), mTrackThickness);
                trackRect.stretchWidth(-maxThumbSize.x());
			}
			else
			{
				trackRect = MLRect(0, 0, mTrackThickness, bb.height());
                trackRect.stretchHeight(-maxThumbSize.y());
			}
			
			// get track position relative to bounds rect
			if (multi)
			{
				trackRect.setCenter(bb.getSize());
			}
			else
			{
				if(mTopLeft)
				{
					if (isHorizontal())
					{
						trackRect.setBottom(bb.height() - padding);
					}
					else
					{
						trackRect.setRight(bb.width() - padding);
					}
				}
				else
				{
					if (isHorizontal())
					{
						trackRect.setTop(padding);
					}
					else
					{
						trackRect.setLeft(padding);
					}
				}
			}
		}

		pC->setBounds(cBounds);
		
        // get display scale
        int displayScale = 1;
        
        // TODO fix display scale and move code into MLWidget 
        /*
        // get the top-level window containing this Component
        ComponentPeer* peer = getPeer();
        if(peer)
        {
            Rectangle<int> peerBounds = peer->getBounds();
            const Desktop::Displays::Display& d = Desktop::getInstance().getDisplays().getDisplayContaining(peerBounds.getCentre());
            displayScale = (int)d.scale;
             if(getWidgetName() == "key_voices")
             {
                debug() << "RESIZEWIDGET: " << getWidgetName() << ", " << b << "\n";
                debug() << "    peer bounds: " << juceToMLRect(peerBounds) << "\n";
                debug() << "    display scale: " << displayScale << "\n";
            }
        }
         */
        
		// make compositing images
		if ((width > 0) && (height > 0))
		{
			int compWidth = getWidth();
			int compHeight = getHeight();
			mParameterImage = Image(Image::ARGB, compWidth, compHeight, true, SoftwareImageType());
			mParameterImage.clear(Rectangle<int>(0, 0, compWidth, compHeight), Colours::transparentBlack);
			mThumbImage = Image(Image::ARGB, compWidth, compHeight, true, SoftwareImageType());
			mThumbImage.clear(Rectangle<int>(0, 0, compWidth, compHeight), Colours::transparentBlack);
            
			mStaticImage = Image(Image::ARGB, compWidth * displayScale, compHeight*displayScale, true, SoftwareImageType());
			mStaticImage.clear(Rectangle<int>(0, 0, compWidth, compHeight), Colours::transparentBlack);
            
		}
		
		mParameterLayerNeedsRedraw = mThumbLayerNeedsRedraw = mStaticLayerNeedsRedraw = true;
		resized();
	}
}
