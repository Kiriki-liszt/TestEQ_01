//------------------------------------------------------------------------
// Copyright(c) 2024 yg331.
//------------------------------------------------------------------------

#include "RFEQ_controller.h"
#include "RFEQ_cids.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "pluginterfaces/base/ustring.h"
#include "base/source/fstreamer.h"

//#include "vstgui/vstgui.h"
#include "vstgui/vstgui_uidescription.h"
#include "vstgui/uidescription/detail/uiviewcreatorattributes.h"

using namespace Steinberg;

namespace VSTGUI {
	//------------------------------------------------------------------------
	//  TextEdit with Knob mouse control
	//------------------------------------------------------------------------
	static constexpr CViewAttributeID kCKnobTextMouseStateAttribute = 'ktms';
	static const std::string kAttrMinPlain = "min-plain";
	static const std::string kAttrMaxPlain = "max-plain";
	static const std::string kAttrLogScale = "Log-Scale";
	//------------------------------------------------------------------------
#if TARGET_OS_IPHONE
	static const float kCKnobTextRange = 300.f;
#else
	static const float kCKnobTextRange = 200.f;
#endif
	struct MyKnobText::MouseEditingState
	{
		CPoint firstPoint;
		CPoint lastPoint;
		float startValue;
		float entryState;
		float range;
		float coef;
		CButtonState oldButton;
		bool modeLinear;
	};
	MyKnobText::MyKnobText(
		const CRect& size,
		IControlListener* listener,
		int32_t tag,
		UTF8StringPtr txt,
		CBitmap* background,
		const int32_t style
	)
		: CTextEdit(size, listener, tag, txt, background)
	{
		rangeAngle = 1.f;
		setStartAngle((float)(3.f * Constants::quarter_pi));
		setRangeAngle((float)(3.f * Constants::half_pi));
		zoomFactor = 1.5f;
		minPlain = 20.0;
		maxPlain = 22000.0;
		logScale = false;
	}

	MyKnobText::MyKnobText(const MyKnobText& v)
		: CTextEdit(v)
		, startAngle(v.startAngle)
		, rangeAngle(v.rangeAngle)
		, zoomFactor(v.zoomFactor)
		, inset(v.inset)
		, minPlain(v.minPlain)
		, maxPlain(v.maxPlain)
		, logScale(v.logScale)
	{
	}

	void  MyKnobText::valueToPoint(CPoint& point) const {
		float alpha = (value - getMin()) / (getMax() - getMin());
		alpha = startAngle + alpha * rangeAngle;

		CPoint c(getViewSize().getWidth() / 2., getViewSize().getHeight() / 2.);
		double xradius = c.x - inset;
		double yradius = c.y - inset;

		point.x = (CCoord)(c.x + cosf(alpha) * xradius + 0.5f);
		point.y = (CCoord)(c.y + sinf(alpha) * yradius + 0.5f);
	}

	float MyKnobText::valueFromPoint(CPoint& point) const {
		float v;
		double d = rangeAngle * 0.5;
		double a = startAngle + d;

		CPoint c(getViewSize().getWidth() / 2., getViewSize().getHeight() / 2.);
		double xradius = c.x - inset;
		double yradius = c.y - inset;

		double dx = (point.x - c.x) / xradius;
		double dy = (point.y - c.y) / yradius;

		double alpha = atan2(dy, dx) - a;
		while (alpha >= Constants::pi)
			alpha -= Constants::double_pi;
		while (alpha < -Constants::pi)
			alpha += Constants::double_pi;

		if (d < 0.0)
			alpha = -alpha;

		if (alpha > d)
			v = getMax();
		else if (alpha < -d)
			v = getMin();
		else
		{
			v = float(0.5 + alpha / rangeAngle);
			v = getMin() + (v * getRange());
		}

		return v;
	};

	// overrides
	void MyKnobText::setText(const UTF8String& txt) {
		float val = getValue();
		val = atof(txt.getString().c_str());
		if (getLogScale())
			val = log(val / getMinPlain()) / log(getMaxPlain() / getMinPlain());
		else
			val = (val - getMinPlain()) / (getMaxPlain() - getMinPlain());
		CTextLabel::setValue(val);
		CTextEdit::setText(txt);
	};

	void MyKnobText::onMouseWheelEvent(MouseWheelEvent& event) {
		onMouseWheelEditing(this);

		float v = getValueNormalized();
		if (buttonStateFromEventModifiers(event.modifiers) & kZoomModifier)
			v += 0.1f * static_cast<float> (event.deltaY) * getWheelInc();
		else
			v += static_cast<float> (event.deltaY) * getWheelInc();
		setValueNormalized(v);

		if (isDirty())
		{
			invalid();
			valueChanged();
		}
		event.consumed = true;
	};

	void MyKnobText::onKeyboardEvent(KeyboardEvent& event) {

		if (!platformControl || event.type != EventType::KeyDown)
			return;

		if (event.virt == VirtualKey::Escape)
		{
			bWasReturnPressed = false;
			platformControl->setText(text);
			getFrame()->setFocusView(nullptr);
			looseFocus();
			event.consumed = true;
		}
		else if (event.virt == VirtualKey::Return)
		{
			bWasReturnPressed = true;
			getFrame()->setFocusView(nullptr);
			looseFocus();
			event.consumed = true;
		}


		if (event.type != EventType::KeyDown)
			return;
		switch (event.virt)
		{
		case VirtualKey::Up:
		case VirtualKey::Right:
		case VirtualKey::Down:
		case VirtualKey::Left:
		{
			float distance = 1.f;
			if (event.virt == VirtualKey::Down || event.virt == VirtualKey::Left)
				distance = -distance;

			float v = getValueNormalized();
			if (buttonStateFromEventModifiers(event.modifiers) & kZoomModifier)
				v += 0.1f * distance * getWheelInc();
			else
				v += distance * getWheelInc();
			setValueNormalized(v);

			if (isDirty())
			{
				invalid();
				beginEdit();
				valueChanged();
				endEdit();
			}
			event.consumed = true;
		}
		case VirtualKey::Escape:
		{
			if (isEditing())
			{
				onMouseCancel();
				event.consumed = true;
			}
			break;
		}
		default: return;
		}
	}

	void MyKnobText::setViewSize(const CRect& rect, bool invalid)
	{
		CControl::setViewSize(rect, invalid);
		compute();
	}

	bool MyKnobText::sizeToFit() {
		if (getDrawBackground())
		{
			CRect vs(getViewSize());
			vs.setWidth(getDrawBackground()->getWidth());
			vs.setHeight(getDrawBackground()->getHeight());
			setViewSize(vs);
			setMouseableArea(vs);
			return true;
		}
		return false;
	}

	CMouseEventResult MyKnobText::onMouseDown(CPoint& where, const CButtonState& buttons) {
		if (!buttons.isLeftButton())
			return kMouseEventNotHandled;

		if (getFrame()->getFocusView() != this)
		{
			if (isDoubleClickStyle() && (buttons & kDoubleClick))
			{
				takeFocus();
				return kMouseDownEventHandledButDontNeedMovedOrUpEvents;
			}

			// takeFocus();
		}

		CMouseWheelEditingSupport::invalidMouseWheelEditTimer(this);
		beginEdit();

		auto& mouseState = getMouseEditingState();
		mouseState.firstPoint = where;
		mouseState.lastPoint(-1, -1);
		mouseState.startValue = getOldValue();

		mouseState.modeLinear = false;
		mouseState.entryState = value;
		mouseState.range = kCKnobTextRange;
		mouseState.coef = (getMax() - getMin()) / mouseState.range;
		mouseState.oldButton = buttons;

		int32_t mode = kCircularMode;
		int32_t newMode = getFrame()->getKnobMode();
		if (kLinearMode == newMode)
		{
			if (!(buttons & kAlt))
				mode = newMode;
		}
		else if (buttons & kAlt)
		{
			mode = kLinearMode;
		}

		if (mode == kLinearMode)
		{
			if (buttons & kZoomModifier)
				mouseState.range *= zoomFactor;
			mouseState.lastPoint = where;
			mouseState.modeLinear = true;
			mouseState.coef = (getMax() - getMin()) / mouseState.range;
		}
		else
		{
			CPoint where2(where);
			where2.offset(-getViewSize().left, -getViewSize().top);
			mouseState.startValue = valueFromPoint(where2);
			mouseState.lastPoint = where;
		}

		return onMouseMoved(where, buttons);
	};

	CMouseEventResult MyKnobText::onMouseUp(CPoint& where, const CButtonState& buttons) {
		if (isEditing())
		{
			endEdit();
			clearMouseEditingState();
		}
		return kMouseEventHandled;
	};

	CMouseEventResult MyKnobText::onMouseMoved(CPoint& where, const CButtonState& buttons) {
		if (buttons.isLeftButton() && isEditing())
		{
			auto& mouseState = getMouseEditingState();

			float middle = (getMax() - getMin()) * 0.5f;

			if (where != mouseState.lastPoint)
			{
				mouseState.lastPoint = where;
				if (mouseState.modeLinear)
				{
					CCoord diff = (mouseState.firstPoint.y - where.y) + (where.x - mouseState.firstPoint.x);
					if (buttons != mouseState.oldButton)
					{
						mouseState.range = kCKnobTextRange;
						if (buttons & kZoomModifier)
							mouseState.range *= zoomFactor;

						float coef2 = (getMax() - getMin()) / mouseState.range;
						mouseState.entryState += (float)(diff * (mouseState.coef - coef2));
						mouseState.coef = coef2;
						mouseState.oldButton = buttons;
					}
					//value = (float)(mouseState.entryState + diff * mouseState.coef);
					setValue((float)(mouseState.entryState + diff * mouseState.coef));
					bounceValue();
				}
				else
				{
					where.offset(-getViewSize().left, -getViewSize().top);
					//value = valueFromPoint(where); 
					setValue(valueFromPoint(where));
					if (mouseState.startValue - value > middle)
						setValue(getMax()); // value = getMax();
					else if (value - mouseState.startValue > middle)
						setValue(getMin()); // value = getMin();
					else
						mouseState.startValue = value;
				}
				if (value != getOldValue())
					valueChanged();
				if (isDirty())
					invalid();
			}
			return kMouseEventHandled;
		}
		return kMouseEventNotHandled;
	};

	CMouseEventResult MyKnobText::onMouseCancel() {
		if (isEditing())
		{
			auto& mouseState = getMouseEditingState();
			//value = mouseState.startValue;
			setValue(mouseState.startValue);
			if (isDirty())
			{
				valueChanged();
				invalid();
			}
			endEdit();
			clearMouseEditingState();
		}
		return kMouseEventHandled;
	};

	auto MyKnobText::getMouseEditingState() -> MouseEditingState& {
		MouseEditingState* state = nullptr;
		if (!getAttribute(kCKnobTextMouseStateAttribute, state))
		{
			state = new MouseEditingState;
			setAttribute(kCKnobTextMouseStateAttribute, state);
		}
		return *state;
	};

	void MyKnobText::clearMouseEditingState() {
		MouseEditingState* state = nullptr;
		if (!getAttribute(kCKnobTextMouseStateAttribute, state))
			return;
		delete state;
		removeAttribute(kCKnobTextMouseStateAttribute);
	};

	//------------------------------------------------------------------------
	//  Factory for TextEdit
	//------------------------------------------------------------------------
	class MyKnobTextFactory : public ViewCreatorAdapter
	{
	public:
		//register this class with the view factory
		MyKnobTextFactory() { UIViewFactory::registerViewCreator(*this); }

		//return an unique name here
		IdStringPtr getViewName() const override { return "KnobText"; }

		//return the name here from where your custom view inherites.
		//	Your view automatically supports the attributes from it.
		IdStringPtr getBaseViewName() const override { return UIViewCreator::kCTextEdit; }

		//create your view here.
		//	Note you don't need to apply attributes here as
		//	the apply method will be called with this new view
		CView* create(const UIAttributes& attributes, const IUIDescription* description) const override
		{
			return new MyKnobText(CRect(0, 0, 100, 20), nullptr, -1, nullptr, nullptr);
		}
		bool apply(
			CView* view,
			const UIAttributes& attributes,
			const IUIDescription* description) const
		{
			auto* KnobText = dynamic_cast<MyKnobText*> (view);

			if (!KnobText)
				return false;

			double d;
			if (attributes.getDoubleAttribute(UIViewCreator::kAttrAngleStart, d))
			{
				// convert from degree
				d = d / 180.f * static_cast<float> (Constants::pi);
				KnobText->setStartAngle(static_cast<float> (d));
			}
			if (attributes.getDoubleAttribute(UIViewCreator::kAttrAngleRange, d))
			{
				// convert from degree
				d = d / 180.f * static_cast<float> (Constants::pi);
				KnobText->setRangeAngle(static_cast<float> (d));
			}
			if (attributes.getDoubleAttribute(UIViewCreator::kAttrValueInset, d))
				KnobText->setInsetValue(d);
			if (attributes.getDoubleAttribute(UIViewCreator::kAttrZoomFactor, d))
				KnobText->setZoomFactor(static_cast<float> (d));
			if (attributes.getDoubleAttribute(kAttrMinPlain, d))
				KnobText->setMinPlain(static_cast<float> (d));
			if (attributes.getDoubleAttribute(kAttrMaxPlain, d))
				KnobText->setMaxPlain(static_cast<float> (d));

			bool b;
			if (attributes.getBooleanAttribute(kAttrLogScale, b))
				KnobText->setLogScale(b);

			return true;
		}

		bool getAttributeNames(StringList& attributeNames) const
		{
			attributeNames.emplace_back(UIViewCreator::kAttrAngleStart);
			attributeNames.emplace_back(UIViewCreator::kAttrAngleRange);
			attributeNames.emplace_back(UIViewCreator::kAttrValueInset);
			attributeNames.emplace_back(UIViewCreator::kAttrZoomFactor);
			attributeNames.emplace_back(kAttrMinPlain);
			attributeNames.emplace_back(kAttrMaxPlain);
			attributeNames.emplace_back(kAttrLogScale);
			return true;
		}

		AttrType getAttributeType(const std::string& attributeName) const
		{
			if (attributeName == UIViewCreator::kAttrAngleStart)
				return kFloatType;
			if (attributeName == UIViewCreator::kAttrAngleRange)
				return kFloatType;
			if (attributeName == UIViewCreator::kAttrValueInset)
				return kFloatType;
			if (attributeName == UIViewCreator::kAttrZoomFactor)
				return kFloatType;
			if (attributeName == kAttrMinPlain)
				return kFloatType;
			if (attributeName == kAttrMaxPlain)
				return kFloatType; 
			if (attributeName == kAttrLogScale)
				return kBooleanType;
			return kUnknownType;
		}

		//------------------------------------------------------------------------
		bool getAttributeValue(
			CView* view,
			const string& attributeName,
			string& stringValue,
			const IUIDescription* desc) const
		{
			auto* KnobText = dynamic_cast<MyKnobText*> (view);
			if (!KnobText)
				return false;

			if (attributeName == UIViewCreator::kAttrAngleStart)
			{
				stringValue =
					UIAttributes::doubleToString((KnobText->getStartAngle() / Constants::pi * 180.), 5);
				return true;
			}
			if (attributeName == UIViewCreator::kAttrAngleRange)
			{
				stringValue =
					UIAttributes::doubleToString((KnobText->getRangeAngle() / Constants::pi * 180.), 5);
				return true;
			}
			if (attributeName == UIViewCreator::kAttrValueInset)
			{
				stringValue = UIAttributes::doubleToString(KnobText->getInsetValue());
				return true;
			}
			if (attributeName == kAttrMinPlain)
			{
				stringValue = UIAttributes::doubleToString(KnobText->getMinPlain());
				return true;
			}
			if (attributeName == kAttrMaxPlain)
			{
				stringValue = UIAttributes::doubleToString(KnobText->getMaxPlain());
				return true;
			}
			if (attributeName == kAttrLogScale)
			{
				stringValue = KnobText->getLogScale() ? UIViewCreator::strTrue : UIViewCreator::strFalse;
				return true;
			}
			return false; 
		}
	};

	//create a static instance so that it registers itself with the view factory
	MyKnobTextFactory __gMyMyKnobTextFactory;
} // namespace VSTGUI

namespace yg331 {


//------------------------------------------------------------------------
// LogRangeParameter Declaration
//------------------------------------------------------------------------
class LogRangeParameter : public Vst::RangeParameter
{
public:
	using RangeParameter::RangeParameter;
	Vst::ParamValue toPlain(Vst::ParamValue _valueNormalized) const SMTG_OVERRIDE;
	Vst::ParamValue toNormalized(Vst::ParamValue plainValue) const SMTG_OVERRIDE;
	void toString(Vst::ParamValue _valueNormalized, Vst::String128 string) const SMTG_OVERRIDE;
};
//------------------------------------------------------------------------
// LogRangeParameter Implementation
//------------------------------------------------------------------------
Vst::ParamValue LogRangeParameter::toPlain(Vst::ParamValue _valueNormalized) const
{
	double FREQ_LOG_MAX = log(getMax() / getMin());
	double tmp = getMin() * exp(FREQ_LOG_MAX * _valueNormalized);
	double freq = (std::max)((std::min)(tmp, getMax()), getMin());
	return freq;
	//return _valueNormalized * (getMax() - getMin()) + getMin();
}

//------------------------------------------------------------------------
Vst::ParamValue LogRangeParameter::toNormalized(Vst::ParamValue plainValue) const
{
	SMTG_ASSERT(getMax() - getMin() != 0);
	double FREQ_LOG_MAX = log(getMax() / getMin());
	return log(plainValue / getMin()) / FREQ_LOG_MAX;
	//return (plainValue - getMin()) / (getMax() - getMin());
}

void LogRangeParameter::toString(Vst::ParamValue _valueNormalized, Vst::String128 string) const
{
	{
		//Parameter::toString(toPlain(_valueNormalized), string);
		UString wrapper(string, str16BufferSize(Vst::String128));
		{
			if (!wrapper.printFloat(toPlain(_valueNormalized), precision))
				string[0] = 0;
			//wrapper.append(STR16(" "));
			//wrapper.append(getInfo().units);
		}
	}
}

//------------------------------------------------------------------------
// LinRangeParameter Declaration
//------------------------------------------------------------------------
class LinRangeParameter : public Vst::RangeParameter
{
public:
	using RangeParameter::RangeParameter;
	void toString(Vst::ParamValue _valueNormalized, Vst::String128 string) const SMTG_OVERRIDE;
};
//------------------------------------------------------------------------
// LinRangeParameter Implementation
//------------------------------------------------------------------------
void LinRangeParameter::toString(Vst::ParamValue _valueNormalized, Vst::String128 string) const
{
	{
		//Parameter::toString(toPlain(_valueNormalized), string);
		UString wrapper(string, str16BufferSize(Vst::String128));
		{
			if (!wrapper.printFloat(toPlain(_valueNormalized), precision))
				string[0] = 0;
			//wrapper.append(STR16(" "));
			//wrapper.append(getInfo().units);
		}
	}
}

//------------------------------------------------------------------------
// RFEQ_Controller Implementation
//------------------------------------------------------------------------
tresult PLUGIN_API RFEQ_Controller::initialize (FUnknown* context)
{
	// Here the Plug-in will be instantiated

	//---do not forget to call parent ------
	tresult result = EditControllerEx1::initialize (context);
	if (result != kResultOk)
	{
		return result;
	}

	// Here you could register some parameters

	int32 stepCount;
	int32 flags;
	int32 tag;
	Vst::ParamValue defaultVal;
	Vst::ParamValue defaultPlain;
	Vst::ParamValue minPlain;
	Vst::ParamValue maxPlain;

	tag = kParamBypass;
	stepCount = 1;
	defaultVal = Init_Bypass ? 1 : 0;
	flags = Vst::ParameterInfo::kCanAutomate | Vst::ParameterInfo::kIsBypass;
	parameters.addParameter(STR16("Bypass"), nullptr, stepCount, defaultVal, flags, tag);

	if (zoomFactors.empty())
	{
		Vst::ParamValue zoom_coef = 1.0;
		zoomFactors.push_back(ZoomFactor(STR("50%"),  zoom_coef * 0.5));  // 0/6
		zoomFactors.push_back(ZoomFactor(STR("75%"),  zoom_coef * 0.75)); // 1/6
		zoomFactors.push_back(ZoomFactor(STR("100%"), zoom_coef * 1.0));  // 2/6
		zoomFactors.push_back(ZoomFactor(STR("125%"), zoom_coef * 1.25)); // 3/6
		zoomFactors.push_back(ZoomFactor(STR("150%"), zoom_coef * 1.5));  // 4/6
		zoomFactors.push_back(ZoomFactor(STR("175%"), zoom_coef * 1.75)); // 5/6
		zoomFactors.push_back(ZoomFactor(STR("200%"), zoom_coef * 2.0));  // 6/6
	}

	auto zoomParameter = new Vst::StringListParameter(STR("Zoom"), kParamZoom);
	for (auto it = zoomFactors.begin(), end = zoomFactors.end(); it != end; ++it)
	{
		zoomParameter->appendString(it->title);
	}
	zoomParameter->setNormalized(zoomParameter->toNormalized(Init_Zoom)); // toNorm(2) == 100%
	zoomParameter->addDependent(this);
	parameters.addParameter(zoomParameter);

	flags = Vst::ParameterInfo::kCanAutomate;

	minPlain = SVF::getdBMin();
	maxPlain = SVF::getdBMax();
	defaultPlain = 0.0;
	stepCount = 0;

	tag = kParamLevel;
	auto* ParamIn = new LinRangeParameter(STR16("Level"), tag, STR16("dB"), minPlain, maxPlain, defaultPlain, stepCount, flags);
	ParamIn->setPrecision(2);
	parameters.addParameter(ParamIn);

	tag = kParamOutput;
	auto* ParamOut = new Vst::RangeParameter(STR16("Output"), tag, STR16("dB"), minPlain, maxPlain, defaultPlain, stepCount, flags);
	ParamOut->setPrecision(2);
	parameters.addParameter(ParamOut);


	stepCount = 1;
	defaultVal = Init_Bypass ? 1 : 0;
	parameters.addParameter(STR16("Band1_In"), nullptr, stepCount, defaultVal, flags, kParamBand1_In);
	parameters.addParameter(STR16("Band2_In"), nullptr, stepCount, defaultVal, flags, kParamBand2_In);
	parameters.addParameter(STR16("Band3_In"), nullptr, stepCount, defaultVal, flags, kParamBand3_In);
	parameters.addParameter(STR16("Band4_In"), nullptr, stepCount, defaultVal, flags, kParamBand4_In);
	parameters.addParameter(STR16("Band5_In"), nullptr, stepCount, defaultVal, flags, kParamBand5_In);


	minPlain = SVF::getdBMin();
	maxPlain = SVF::getdBMax();
	defaultPlain = 0.0;
	stepCount = 0;

	auto* Band1_dB = new LinRangeParameter(STR("Band1_dB"), kParamBand1_dB, STR("dB"), minPlain, maxPlain, defaultPlain, stepCount, flags);
	auto* Band2_dB = new LinRangeParameter(STR("Band2_dB"), kParamBand2_dB, STR("dB"), minPlain, maxPlain, defaultPlain, stepCount, flags);
	auto* Band3_dB = new LinRangeParameter(STR("Band3_dB"), kParamBand3_dB, STR("dB"), minPlain, maxPlain, defaultPlain, stepCount, flags);
	auto* Band4_dB = new LinRangeParameter(STR("Band4_dB"), kParamBand4_dB, STR("dB"), minPlain, maxPlain, defaultPlain, stepCount, flags);
	auto* Band5_dB = new LinRangeParameter(STR("Band5_dB"), kParamBand5_dB, STR("dB"), minPlain, maxPlain, defaultPlain, stepCount, flags);
	Band1_dB->setPrecision(2);
	Band2_dB->setPrecision(2);
	Band3_dB->setPrecision(2);
	Band4_dB->setPrecision(2);
	Band5_dB->setPrecision(2);
	parameters.addParameter(Band1_dB);
	parameters.addParameter(Band2_dB);
	parameters.addParameter(Band3_dB);
	parameters.addParameter(Band4_dB);
	parameters.addParameter(Band5_dB);

	minPlain = SVF::getFreqMin();
	maxPlain = SVF::getFreqMax();
	stepCount = 0;

	auto* Band1_Hz = new LogRangeParameter(STR("Band1_Hz"), kParamBand1_Hz, STR("Hz"), minPlain, maxPlain, SVF::Init_Band1_Hz(), stepCount, flags);
	auto* Band2_Hz = new LogRangeParameter(STR("Band2_Hz"), kParamBand2_Hz, STR("Hz"), minPlain, maxPlain, SVF::Init_Band2_Hz(), stepCount, flags);
	auto* Band3_Hz = new LogRangeParameter(STR("Band3_Hz"), kParamBand3_Hz, STR("Hz"), minPlain, maxPlain, SVF::Init_Band3_Hz(), stepCount, flags);
	auto* Band4_Hz = new LogRangeParameter(STR("Band4_Hz"), kParamBand4_Hz, STR("Hz"), minPlain, maxPlain, SVF::Init_Band4_Hz(), stepCount, flags);
	auto* Band5_Hz = new LogRangeParameter(STR("Band5_Hz"), kParamBand5_Hz, STR("Hz"), minPlain, maxPlain, SVF::Init_Band5_Hz(), stepCount, flags);
	Band1_Hz->setPrecision(0);
	Band2_Hz->setPrecision(0);
	Band3_Hz->setPrecision(0);
	Band4_Hz->setPrecision(0);
	Band5_Hz->setPrecision(0);
	parameters.addParameter(Band1_Hz);
	parameters.addParameter(Band2_Hz);
	parameters.addParameter(Band3_Hz);
	parameters.addParameter(Band4_Hz);
	parameters.addParameter(Band5_Hz);


	minPlain = SVF::getQMin();
	maxPlain = SVF::getQMax();
	defaultPlain = 1.414;
	stepCount = 0;

	auto* Band1_Q = new LogRangeParameter(STR16("Band1_Q"), kParamBand1_Q, STR16("Q"), minPlain, maxPlain, defaultPlain, stepCount, flags);
	auto* Band2_Q = new LogRangeParameter(STR16("Band2_Q"), kParamBand2_Q, STR16("Q"), minPlain, maxPlain, defaultPlain, stepCount, flags);
	auto* Band3_Q = new LogRangeParameter(STR16("Band3_Q"), kParamBand3_Q, STR16("Q"), minPlain, maxPlain, defaultPlain, stepCount, flags);
	auto* Band4_Q = new LogRangeParameter(STR16("Band4_Q"), kParamBand4_Q, STR16("Q"), minPlain, maxPlain, defaultPlain, stepCount, flags);
	auto* Band5_Q = new LogRangeParameter(STR16("Band5_Q"), kParamBand5_Q, STR16("Q"), minPlain, maxPlain, defaultPlain, stepCount, flags);
	Band1_Q->setPrecision(2);
	Band2_Q->setPrecision(2);
	Band3_Q->setPrecision(2);
	Band4_Q->setPrecision(2);
	Band5_Q->setPrecision(2);
	parameters.addParameter(Band1_Q);
	parameters.addParameter(Band2_Q);
	parameters.addParameter(Band3_Q);
	parameters.addParameter(Band4_Q);
	parameters.addParameter(Band5_Q);

	auto* Band1_Type = new Vst::StringListParameter(STR16("Band1_Type"), kParamBand1_Type, STR16(""), flags);
	auto* Band2_Type = new Vst::StringListParameter(STR16("Band2_Type"), kParamBand2_Type, STR16(""), flags);
	auto* Band3_Type = new Vst::StringListParameter(STR16("Band3_Type"), kParamBand3_Type, STR16(""), flags);
	auto* Band4_Type = new Vst::StringListParameter(STR16("Band4_Type"), kParamBand4_Type, STR16(""), flags);
	auto* Band5_Type = new Vst::StringListParameter(STR16("Band5_Type"), kParamBand5_Type, STR16(""), flags);

	for (int i = 0; i < SVF::kFltNum + 1; i++) {
		Band1_Type->appendString(Filter_Types[i]);
		Band2_Type->appendString(Filter_Types[i]);
		Band3_Type->appendString(Filter_Types[i]);
		Band4_Type->appendString(Filter_Types[i]);
		Band5_Type->appendString(Filter_Types[i]);
	}

	Band1_Type->setNormalized(SVF::_Type_to_norm(SVF::kBell));
	Band2_Type->setNormalized(SVF::_Type_to_norm(SVF::kBell));
	Band3_Type->setNormalized(SVF::_Type_to_norm(SVF::kBell));
	Band4_Type->setNormalized(SVF::_Type_to_norm(SVF::kBell));
	Band5_Type->setNormalized(SVF::_Type_to_norm(SVF::kBell));

	parameters.addParameter(Band1_Type);
	parameters.addParameter(Band2_Type);
	parameters.addParameter(Band3_Type);
	parameters.addParameter(Band4_Type);
	parameters.addParameter(Band5_Type);


	auto* Band1_Order = new Vst::StringListParameter(STR16("Band1_Order"), kParamBand1_Order, STR16(""), flags);
	auto* Band2_Order = new Vst::StringListParameter(STR16("Band2_Order"), kParamBand2_Order, STR16(""), flags);
	auto* Band3_Order = new Vst::StringListParameter(STR16("Band3_Order"), kParamBand3_Order, STR16(""), flags);
	auto* Band4_Order = new Vst::StringListParameter(STR16("Band4_Order"), kParamBand4_Order, STR16(""), flags);
	auto* Band5_Order = new Vst::StringListParameter(STR16("Band5_Order"), kParamBand5_Order, STR16(""), flags);
	for (int i = 0; i < SVF::kOrderNum + 1; i++) {
		Band1_Order->appendString(Filter_Order[i]);
		Band2_Order->appendString(Filter_Order[i]);
		Band3_Order->appendString(Filter_Order[i]);
		Band4_Order->appendString(Filter_Order[i]);
		Band5_Order->appendString(Filter_Order[i]);
	}
	Band1_Order->setNormalized(SVF::_Order_to_norm(SVF::_12dBoct));
	Band2_Order->setNormalized(SVF::_Order_to_norm(SVF::_12dBoct));
	Band3_Order->setNormalized(SVF::_Order_to_norm(SVF::_12dBoct));
	Band4_Order->setNormalized(SVF::_Order_to_norm(SVF::_12dBoct));
	Band5_Order->setNormalized(SVF::_Order_to_norm(SVF::_12dBoct));
	parameters.addParameter(Band1_Order);
	parameters.addParameter(Band2_Order);
	parameters.addParameter(Band3_Order);
	parameters.addParameter(Band4_Order);
	parameters.addParameter(Band5_Order);

	return result;
}

//------------------------------------------------------------------------
tresult PLUGIN_API RFEQ_Controller::terminate ()
{
	// Here the Plug-in will be de-instantiated, last possibility to remove some memory!

	//---do not forget to call parent ------
	return EditControllerEx1::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API RFEQ_Controller::setComponentState (IBStream* state)
{
	// Here you get the state of the component (Processor part)
	if (!state)
		return kResultFalse;

	IBStreamer streamer(state, kLittleEndian);

	int32           savedBypass = 0;
	Vst::ParamValue savedZoom   = 0.0;
	Vst::ParamValue savedLevel  = 0.0;
	Vst::ParamValue savedOutput = 0.0;

	ParamBand_Array savedBand1_Array = { 0.0, };
	ParamBand_Array savedBand2_Array = { 0.0, };
	ParamBand_Array savedBand3_Array = { 0.0, };
	ParamBand_Array savedBand4_Array = { 0.0, };
	ParamBand_Array savedBand5_Array = { 0.0, };

	if (streamer.readInt32 (savedBypass) == false) return kResultFalse;
	if (streamer.readDouble(savedZoom  ) == false) return kResultFalse;
	if (streamer.readDouble(savedLevel ) == false) return kResultFalse;
	if (streamer.readDouble(savedOutput) == false) return kResultFalse;

	if (streamer.readDoubleArray(savedBand1_Array, ParamArray_size) == false) return kResultFalse;
	if (streamer.readDoubleArray(savedBand2_Array, ParamArray_size) == false) return kResultFalse;
	if (streamer.readDoubleArray(savedBand3_Array, ParamArray_size) == false) return kResultFalse;
	if (streamer.readDoubleArray(savedBand4_Array, ParamArray_size) == false) return kResultFalse;
	if (streamer.readDoubleArray(savedBand5_Array, ParamArray_size) == false) return kResultFalse;

	setParamNormalized(kParamBypass, savedBypass ? 1 : 0);
	setParamNormalized(kParamZoom,   savedZoom);
	setParamNormalized(kParamLevel,  savedLevel);
	setParamNormalized(kParamOutput, savedOutput);

	setParamNormalized(kParamBand1_In, savedBand1_Array[ParamArray_In] ? 1 : 0);
	setParamNormalized(kParamBand2_In, savedBand2_Array[ParamArray_In] ? 1 : 0);
	setParamNormalized(kParamBand3_In, savedBand3_Array[ParamArray_In] ? 1 : 0);
	setParamNormalized(kParamBand4_In, savedBand4_Array[ParamArray_In] ? 1 : 0);
	setParamNormalized(kParamBand5_In, savedBand5_Array[ParamArray_In] ? 1 : 0);
	setParamNormalized(kParamBand1_Hz, savedBand1_Array[ParamArray_Hz]);
	setParamNormalized(kParamBand2_Hz, savedBand2_Array[ParamArray_Hz]);
	setParamNormalized(kParamBand3_Hz, savedBand3_Array[ParamArray_Hz]);
	setParamNormalized(kParamBand4_Hz, savedBand4_Array[ParamArray_Hz]);
	setParamNormalized(kParamBand5_Hz, savedBand5_Array[ParamArray_Hz]);
	setParamNormalized(kParamBand1_Q, savedBand1_Array[ParamArray_Q]);
	setParamNormalized(kParamBand2_Q, savedBand2_Array[ParamArray_Q]);
	setParamNormalized(kParamBand3_Q, savedBand3_Array[ParamArray_Q]);
	setParamNormalized(kParamBand4_Q, savedBand4_Array[ParamArray_Q]);
	setParamNormalized(kParamBand5_Q, savedBand5_Array[ParamArray_Q]);
	setParamNormalized(kParamBand1_dB, savedBand1_Array[ParamArray_dB]);
	setParamNormalized(kParamBand2_dB, savedBand2_Array[ParamArray_dB]);
	setParamNormalized(kParamBand3_dB, savedBand3_Array[ParamArray_dB]);
	setParamNormalized(kParamBand4_dB, savedBand4_Array[ParamArray_dB]);
	setParamNormalized(kParamBand5_dB, savedBand5_Array[ParamArray_dB]);
	setParamNormalized(kParamBand1_Type, savedBand1_Array[ParamArray_Type]);
	setParamNormalized(kParamBand2_Type, savedBand2_Array[ParamArray_Type]);
	setParamNormalized(kParamBand3_Type, savedBand3_Array[ParamArray_Type]);
	setParamNormalized(kParamBand4_Type, savedBand4_Array[ParamArray_Type]);
	setParamNormalized(kParamBand5_Type, savedBand5_Array[ParamArray_Type]);
	setParamNormalized(kParamBand1_Order, savedBand1_Array[ParamArray_Order]);
	setParamNormalized(kParamBand2_Order, savedBand2_Array[ParamArray_Order]);
	setParamNormalized(kParamBand3_Order, savedBand3_Array[ParamArray_Order]);
	setParamNormalized(kParamBand4_Order, savedBand4_Array[ParamArray_Order]);
	setParamNormalized(kParamBand5_Order, savedBand5_Array[ParamArray_Order]);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API RFEQ_Controller::setState (IBStream* state)
{
	/*
	// Here you get the state of the controller
	
	IBStreamer streamer(state, kLittleEndian);

	int8 byteOrder;
	if (streamer.readInt8(byteOrder) == false)
		return kResultFalse;
	if (streamer.readRaw(defaultMessageText, 128 * sizeof(TChar)) == false)
		return kResultFalse;

	// if the byteorder doesn't match, byte swap the text array ...
	if (byteOrder != BYTEORDER)
	{
		for (int32 i = 0; i < 128; i++)
		{
			SWAP_16(defaultMessageText[i])
		}
	}

	// update our editors
	for (auto& uiMessageController : uiMessageControllers)
		uiMessageController->setMessageText(defaultMessageText);
	*/
	return kResultTrue;
}

//------------------------------------------------------------------------
tresult PLUGIN_API RFEQ_Controller::getState (IBStream* state)
{
	// Here you are asked to deliver the state of the controller (if needed)
	// Note: the real state of your plug-in is saved in the processor

	/*
	// here we can save UI settings for example

	// as we save a Unicode string, we must know the byteorder when setState is called

	IBStreamer streamer (state, kLittleEndian);

	int8 byteOrder = BYTEORDER;
	if (streamer.writeInt8 (byteOrder) == false)
		return kResultFalse;

	if (streamer.writeRaw (defaultMessageText, 128 * sizeof (TChar)) == false)
		return kResultFalse;
	*/

	return kResultTrue;
}

//------------------------------------------------------------------------
IPlugView* PLUGIN_API RFEQ_Controller::createView (FIDString name)
{
	// Here the Host wants to open your editor (if you have one)
	if (FIDStringsEqual (name, Vst::ViewType::kEditor))
	{
		// create your editor here and return a IPlugView ptr of it
		auto* view = new VSTGUI::VST3Editor (this, "view", "RFEQ_editor.uidesc");
		view->setZoomFactor(1.0);
		setKnobMode(Steinberg::Vst::KnobModes::kLinearMode);
		main_editor = view;
		return view;
	}
	return nullptr;
}

void PLUGIN_API RFEQ_Controller::update(FUnknown* changedUnknown, int32 message)
{
	EditControllerEx1::update(changedUnknown, message);

	// GUI Resizing
	// check 'zoomtest' code at
	// https://github.com/steinbergmedia/vstgui/tree/vstgui4_10/vstgui/tests/uidescription%20vst3/source

	Vst::Parameter* param = FCast<Vst::Parameter>(changedUnknown);
	if (!param)
		return;

	if (param->getInfo().id == kParamZoom)
	{
		size_t index = static_cast<size_t> (param->toPlain(param->getNormalized()));

		if (index >= zoomFactors.size())
			return;

		if (main_editor)
			main_editor->setZoomFactor(zoomFactors[index].factor);
		
		/*
		for (EditorVector::const_iterator it = editors.begin(), end = editors.end(); it != end; ++it)
		{
			VSTGUI::VST3Editor* editor = dynamic_cast<VSTGUI::VST3Editor*>(*it);
			if (editor)
				editor->setZoomFactor(zoomFactors[index].factor);
		}
		*/
	}
}

//------------------------------------------------------------------------
tresult PLUGIN_API RFEQ_Controller::setParamNormalized (Vst::ParamID tag, Vst::ParamValue value)
{
	// called by host to update your parameters
	tresult result = EditControllerEx1::setParamNormalized (tag, value);
	return result;
}

//------------------------------------------------------------------------
tresult PLUGIN_API RFEQ_Controller::getParamStringByValue (Vst::ParamID tag, Vst::ParamValue valueNormalized, Vst::String128 string)
{
	// called by host to get a string for given normalized value of a specific parameter
	// (without having to set the value!)
	return EditControllerEx1::getParamStringByValue (tag, valueNormalized, string);
}

//------------------------------------------------------------------------
tresult PLUGIN_API RFEQ_Controller::getParamValueByString (Vst::ParamID tag, Vst::TChar* string, Vst::ParamValue& valueNormalized)
{
	// called by host to get a normalized value from a string representation of a specific parameter
	// (without having to set the value!)
	return EditControllerEx1::getParamValueByString (tag, string, valueNormalized);
}


//------------------------------------------------------------------------
// DataExchangeController Implementation
//------------------------------------------------------------------------
tresult PLUGIN_API RFEQ_Controller::notify(Vst::IMessage* message)
{
	if (dataExchange.onMessage(message))
		return kResultTrue;

	if (!message)
		return kInvalidArgument;

	return EditControllerEx1::notify(message);
}


//------------------------------------------------------------------------
void PLUGIN_API RFEQ_Controller::queueOpened(Vst::DataExchangeUserContextID userContextID,
	uint32 blockSize,
	TBool& dispatchOnBackgroundThread)
{
	//FDebugPrint("Data Exchange Queue opened.\n");
}

//------------------------------------------------------------------------
void PLUGIN_API RFEQ_Controller::queueClosed(Vst::DataExchangeUserContextID userContextID)
{
	//FDebugPrint("Data Exchange Queue closed.\n");
}

//------------------------------------------------------------------------
void PLUGIN_API RFEQ_Controller::onDataExchangeBlocksReceived(
	Vst::DataExchangeUserContextID userContextID,
	uint32 numBlocks,
	Vst::DataExchangeBlock* blocks,
	TBool onBackgroundThread
)
{
	for (auto index = 0u; index < numBlocks; ++index)
	{
		auto dataBlock = toDataBlock(blocks[index]);
		/*
		InMeter[0] = dataBlock->inL;
		InMeter[1] = dataBlock->inR;
		OutMeter[0] = dataBlock->outL;
		OutMeter[1] = dataBlock->outR;
		GRMeter = dataBlock->gR;
		if (!vuMeterControllers.empty()) {
			for (auto iter = vuMeterControllers.begin(); iter != vuMeterControllers.end(); iter++) {
				(*iter)->setMeterValue(InMeter[0] * 0.5 + InMeter[1] * 0.5, kIn);
				(*iter)->setMeterValue(OutMeter[0] * 0.5 + OutMeter[1] * 0.5, kOut);
				(*iter)->setMeterValue(GRMeter, kGR);
				(*iter)->setVuMeterValue(
					InMeter[0], InMeter[1],
					OutMeter[0], OutMeter[1],
					GRMeter
				);
			}
		}
		*/
		/*
		FDebugPrint(
			"Received Data Block: SampleRate: %d, SampleSize: %d, NumChannels: %d, NumSamples: %d\n",
			dataBlock->sampleRate,
			static_cast<uint32_t> (dataBlock->sampleSize),
			static_cast<uint32_t> (dataBlock->numChannels),
			static_cast<uint32_t> (dataBlock->numSamples));
		
		FDebugPrint(\
			"Received Data Block: %f %f %f %f %f\n",\
			dataBlock->inL,\
			dataBlock->inR,\
			dataBlock->outL,\
			dataBlock->outR,\
			dataBlock->gR);
		*/
	}
}

//------------------------------------------------------------------------
} // namespace yg331
