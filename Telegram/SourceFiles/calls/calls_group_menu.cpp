/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_group_menu.h"

#include "calls/calls_group_call.h"
#include "calls/calls_group_settings.h"
#include "calls/calls_group_panel.h"
#include "data/data_peer.h"
#include "data/data_group_call.h"
#include "info/profile/info_profile_values.h" // Info::Profile::NameValue.
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/menu/menu.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "lang/lang_keys.h"
#include "base/unixtime.h"
#include "base/timer_rpl.h"
#include "styles/style_calls.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace Calls::Group {
namespace {

constexpr auto kMaxGroupCallLength = 40;

void EditGroupCallTitleBox(
		not_null<Ui::GenericBox*> box,
		const QString &placeholder,
		const QString &title,
		Fn<void(QString)> done) {
	box->setTitle(tr::lng_group_call_edit_title());
	const auto input = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::groupCallField,
		rpl::single(placeholder),
		title));
	input->setMaxLength(kMaxGroupCallLength);
	box->setFocusCallback([=] {
		input->setFocusFast();
	});
	box->addButton(tr::lng_settings_save(), [=] {
		const auto result = input->getLastText().trimmed();
		box->closeBox();
		done(result);
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void StartGroupCallRecordingBox(
		not_null<Ui::GenericBox*> box,
		const QString &title,
		Fn<void(QString)> done) {
	box->setTitle(tr::lng_group_call_recording_start());

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box.get(),
			tr::lng_group_call_recording_start_sure(),
			st::groupCallBoxLabel));

	const auto input = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::groupCallField,
		tr::lng_group_call_recording_start_field(),
		title));
	box->setFocusCallback([=] {
		input->setFocusFast();
	});
	box->addButton(tr::lng_group_call_recording_start_button(), [=] {
		const auto result = input->getLastText().trimmed();
		if (result.isEmpty()) {
			input->showError();
			return;
		}
		box->closeBox();
		done(result);
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void StopGroupCallRecordingBox(
		not_null<Ui::GenericBox*> box,
		Fn<void(QString)> done) {
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box.get(),
			tr::lng_group_call_recording_stop_sure(),
			st::groupCallBoxLabel),
		style::margins(
			st::boxRowPadding.left(),
			st::boxPadding.top(),
			st::boxRowPadding.right(),
			st::boxPadding.bottom()));

	box->addButton(tr::lng_box_ok(), [=] {
		box->closeBox();
		done(QString());
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

[[nodiscard]] auto ToDurationFrom(TimeId startDate) {
	return [=] {
		const auto now = base::unixtime::now();
		const auto elapsed = std::max(now - startDate, 0);
		const auto hours = elapsed / 3600;
		const auto minutes = (elapsed % 3600) / 60;
		const auto seconds = (elapsed % 60);
		return hours
			? QString("%1:%2:%3"
			).arg(hours
			).arg(minutes, 2, 10, QChar('0')
			).arg(seconds, 2, 10, QChar('0'))
			: QString("%1:%2"
			).arg(minutes
			).arg(seconds, 2, 10, QChar('0'));
	};
}

[[nodiscard]] rpl::producer<QString> ToRecordDuration(TimeId startDate) {
	return !startDate
		? (rpl::single(QString()) | rpl::type_erased())
		: rpl::single(
			rpl::empty_value()
		) | rpl::then(base::timer_each(
			crl::time(1000)
		)) | rpl::map(ToDurationFrom(startDate));
}

class JoinAsAction final : public Ui::Menu::ItemBase {
public:
	JoinAsAction(
		not_null<Ui::RpWidget*> parent,
		const style::Menu &st,
		not_null<PeerData*> peer,
		Fn<void()> callback);

	bool isEnabled() const override;
	not_null<QAction*> action() const override;

	void handleKeyPress(not_null<QKeyEvent*> e) override;

protected:
	QPoint prepareRippleStartPosition() const override;
	QImage prepareRippleMask() const override;

	int contentHeight() const override;

private:
	void prepare();
	void paint(Painter &p);

	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	const not_null<PeerData*> _peer;
	std::shared_ptr<Data::CloudImageView> _userpicView;

	Ui::Text::String _text;
	Ui::Text::String _name;
	int _textWidth = 0;
	int _nameWidth = 0;
	const int _height = 0;

};

class RecordingAction final : public Ui::Menu::ItemBase {
public:
	RecordingAction(
		not_null<Ui::RpWidget*> parent,
		const style::Menu &st,
		rpl::producer<QString> text,
		rpl::producer<TimeId> startAtValues,
		Fn<void()> callback);

	bool isEnabled() const override;
	not_null<QAction*> action() const override;

	void handleKeyPress(not_null<QKeyEvent*> e) override;

protected:
	QPoint prepareRippleStartPosition() const override;
	QImage prepareRippleMask() const override;

	int contentHeight() const override;

private:
	void prepare(rpl::producer<QString> text);
	void refreshElapsedText();
	void paint(Painter &p);

	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	TimeId _startAt = 0;
	crl::time _startedAt = 0;
	base::Timer _refreshTimer;

	Ui::Text::String _text;
	int _textWidth = 0;
	QString _elapsedText;
	const int _smallHeight = 0;
	const int _bigHeight = 0;

};

TextParseOptions MenuTextOptions = {
	TextParseLinks | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

JoinAsAction::JoinAsAction(
	not_null<Ui::RpWidget*> parent,
	const style::Menu &st,
	not_null<PeerData*> peer,
	Fn<void()> callback)
: ItemBase(parent, st)
, _dummyAction(new QAction(parent))
, _st(st)
, _peer(peer)
, _height(st::groupCallJoinAsPadding.top()
	+ st::groupCallJoinAsPhotoSize
	+ st::groupCallJoinAsPadding.bottom()) {
	setAcceptBoth(true);
	initResizeHook(parent->sizeValue());
	setClickedCallback(std::move(callback));

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		paint(p);
	}, lifetime());

	enableMouseSelecting();
	prepare();
}

void JoinAsAction::paint(Painter &p) {
	const auto selected = isSelected();
	const auto height = contentHeight();
	if (selected && _st.itemBgOver->c.alpha() < 255) {
		p.fillRect(0, 0, width(), height, _st.itemBg);
	}
	p.fillRect(0, 0, width(), height, selected ? _st.itemBgOver : _st.itemBg);
	if (isEnabled()) {
		paintRipple(p, 0, 0);
	}

	const auto &padding = st::groupCallJoinAsPadding;
	_peer->paintUserpic(
		p,
		_userpicView,
		padding.left(),
		padding.top(),
		st::groupCallJoinAsPhotoSize);
	const auto textLeft = padding.left()
		+ st::groupCallJoinAsPhotoSize
		+ padding.left();
	p.setPen(selected ? _st.itemFgOver : _st.itemFg);
	_text.drawLeftElided(
		p,
		textLeft,
		st::groupCallJoinAsTextTop,
		_textWidth,
		width());
	p.setPen(selected ? _st.itemFgShortcutOver : _st.itemFgShortcut);
	_name.drawLeftElided(
		p,
		textLeft,
		st::groupCallJoinAsNameTop,
		_nameWidth,
		width());
}

void JoinAsAction::prepare() {
	rpl::combine(
		tr::lng_group_call_display_as_header(),
		Info::Profile::NameValue(_peer)
	) | rpl::start_with_next([=](QString text, TextWithEntities name) {
		const auto &padding = st::groupCallJoinAsPadding;
		_text.setMarkedText(_st.itemStyle, { text }, MenuTextOptions);
		_name.setMarkedText(_st.itemStyle, name, MenuTextOptions);
		const auto textWidth = _text.maxWidth();
		const auto nameWidth = _name.maxWidth();
		const auto textLeft = padding.left()
			+ st::groupCallJoinAsPhotoSize
			+ padding.left();
		const auto w = std::clamp(
			(textLeft
				+ std::max(textWidth, nameWidth)
				+ padding.right()),
			_st.widthMin,
			_st.widthMax);
		setMinWidth(w);
		_textWidth = w - textLeft - padding.right();
		_nameWidth = w - textLeft - padding.right();
		update();
	}, lifetime());
}

bool JoinAsAction::isEnabled() const {
	return true;
}

not_null<QAction*> JoinAsAction::action() const {
	return _dummyAction;
}

QPoint JoinAsAction::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

QImage JoinAsAction::prepareRippleMask() const {
	return Ui::RippleAnimation::rectMask(size());
}

int JoinAsAction::contentHeight() const {
	return _height;
}

void JoinAsAction::handleKeyPress(not_null<QKeyEvent*> e) {
	if (!isSelected()) {
		return;
	}
	const auto key = e->key();
	if (key == Qt::Key_Enter || key == Qt::Key_Return) {
		setClicked(Ui::Menu::TriggeredSource::Keyboard);
	}
}

RecordingAction::RecordingAction(
	not_null<Ui::RpWidget*> parent,
	const style::Menu &st,
	rpl::producer<QString> text,
	rpl::producer<TimeId> startAtValues,
	Fn<void()> callback)
: ItemBase(parent, st)
, _dummyAction(new QAction(parent))
, _st(st)
, _refreshTimer([=] { refreshElapsedText(); })
, _smallHeight(st.itemPadding.top()
	+ _st.itemStyle.font->height
	+ st.itemPadding.bottom())
, _bigHeight(st::groupCallRecordingTimerPadding.top()
	+ _st.itemStyle.font->height
	+ st::groupCallRecordingTimerFont->height
	+ st::groupCallRecordingTimerPadding.bottom()) {
	std::move(
		startAtValues
	) | rpl::start_with_next([=](TimeId startAt) {
		_startAt = startAt;
		_startedAt = crl::now();
		_refreshTimer.cancel();
		refreshElapsedText();
		resize(width(), contentHeight());
	}, lifetime());

	setAcceptBoth(true);
	initResizeHook(parent->sizeValue());
	setClickedCallback(std::move(callback));

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		paint(p);
	}, lifetime());

	enableMouseSelecting();
	prepare(std::move(text));
}

void RecordingAction::paint(Painter &p) {
	const auto selected = isSelected();
	const auto height = contentHeight();
	if (selected && _st.itemBgOver->c.alpha() < 255) {
		p.fillRect(0, 0, width(), height, _st.itemBg);
	}
	p.fillRect(0, 0, width(), height, selected ? _st.itemBgOver : _st.itemBg);
	if (isEnabled()) {
		paintRipple(p, 0, 0);
	}
	const auto smallTop = st::groupCallRecordingTimerPadding.top();
	const auto textTop = _startAt ? smallTop : _st.itemPadding.top();
	p.setPen(selected ? _st.itemFgOver : _st.itemFg);
	_text.drawLeftElided(
		p,
		_st.itemPadding.left(),
		textTop,
		_textWidth,
		width());
	if (_startAt) {
		p.setFont(st::groupCallRecordingTimerFont);
		p.setPen(selected ? _st.itemFgShortcutOver : _st.itemFgShortcut);
		p.drawTextLeft(
			_st.itemPadding.left(),
			smallTop + _st.itemStyle.font->height,
			width(),
			_elapsedText);
	}
}

void RecordingAction::refreshElapsedText() {
	const auto now = base::unixtime::now();
	const auto elapsed = std::max(now - _startAt, 0);
	const auto text = !_startAt
		? QString()
		: (elapsed >= 3600)
		? QString("%1:%2:%3"
		).arg(elapsed / 3600
		).arg((elapsed % 3600) / 60, 2, 10, QChar('0')
		).arg(elapsed % 60, 2, 10, QChar('0'))
		: QString("%1:%2"
		).arg(elapsed / 60
		).arg(elapsed % 60, 2, 10, QChar('0'));
	if (_elapsedText != text) {
		_elapsedText = text;
		update();
	}

	const auto nextCall = crl::time(500) - ((crl::now() - _startedAt) % 500);
	_refreshTimer.callOnce(nextCall);
}

void RecordingAction::prepare(rpl::producer<QString> text) {
	refreshElapsedText();

	const auto &padding = _st.itemPadding;
	const auto textWidth1 = _st.itemStyle.font->width(
		tr::lng_group_call_recording_start(tr::now));
	const auto textWidth2 = _st.itemStyle.font->width(
		tr::lng_group_call_recording_stop(tr::now));
	const auto maxWidth = st::groupCallRecordingTimerFont->width("23:59:59");
	const auto w = std::clamp(
		(padding.left()
			+ std::max({ textWidth1, textWidth2, maxWidth })
			+ padding.right()),
		_st.widthMin,
		_st.widthMax);
	setMinWidth(w);

	std::move(text) | rpl::start_with_next([=](QString text) {
		const auto &padding = _st.itemPadding;
		_text.setMarkedText(_st.itemStyle, { text }, MenuTextOptions);
		const auto textWidth = _text.maxWidth();
		_textWidth = w - padding.left() - padding.right();
		update();
	}, lifetime());
}

bool RecordingAction::isEnabled() const {
	return true;
}

not_null<QAction*> RecordingAction::action() const {
	return _dummyAction;
}

QPoint RecordingAction::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

QImage RecordingAction::prepareRippleMask() const {
	return Ui::RippleAnimation::rectMask(size());
}

int RecordingAction::contentHeight() const {
	return _startAt ? _bigHeight : _smallHeight;
}

void RecordingAction::handleKeyPress(not_null<QKeyEvent*> e) {
	if (!isSelected()) {
		return;
	}
	const auto key = e->key();
	if (key == Qt::Key_Enter || key == Qt::Key_Return) {
		setClicked(Ui::Menu::TriggeredSource::Keyboard);
	}
}

base::unique_qptr<Ui::Menu::ItemBase> MakeJoinAsAction(
		not_null<Ui::Menu::Menu*> menu,
		not_null<PeerData*> peer,
		Fn<void()> callback) {
	return base::make_unique_q<JoinAsAction>(
		menu,
		menu->st(),
		peer,
		std::move(callback));
}

base::unique_qptr<Ui::Menu::ItemBase> MakeRecordingAction(
		not_null<Ui::Menu::Menu*> menu,
		rpl::producer<TimeId> startDate,
		Fn<void()> callback) {
	using namespace rpl::mappers;
	return base::make_unique_q<RecordingAction>(
		menu,
		menu->st(),
		rpl::conditional(
			rpl::duplicate(startDate) | rpl::map(!!_1),
			tr::lng_group_call_recording_stop(),
			tr::lng_group_call_recording_start()),
		rpl::duplicate(startDate),
		std::move(callback));
}

base::unique_qptr<Ui::Menu::ItemBase> MakeFinishAction(
		not_null<Ui::Menu::Menu*> menu,
		Fn<void()> callback) {
	return base::make_unique_q<Ui::Menu::Action>(
		menu,
		st::groupCallFinishMenu,
		Ui::Menu::CreateAction(
			menu,
			tr::lng_group_call_end(tr::now),
			std::move(callback)),
		nullptr,
		nullptr);

}

} // namespace

void LeaveBox(
		not_null<Ui::GenericBox*> box,
		not_null<GroupCall*> call,
		bool discardChecked,
		BoxContext context) {
	box->setTitle(tr::lng_group_call_leave_title());
	const auto inCall = (context == BoxContext::GroupCallPanel);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box.get(),
		tr::lng_group_call_leave_sure(),
		(inCall ? st::groupCallBoxLabel : st::boxLabel)));
	const auto discard = call->peer()->canManageGroupCall()
		? box->addRow(object_ptr<Ui::Checkbox>(
			box.get(),
			tr::lng_group_call_end(),
			discardChecked,
			(inCall ? st::groupCallCheckbox : st::defaultBoxCheckbox),
			(inCall ? st::groupCallCheck : st::defaultCheck)),
			style::margins(
				st::boxRowPadding.left(),
				st::boxRowPadding.left(),
				st::boxRowPadding.right(),
				st::boxRowPadding.bottom()))
		: nullptr;
	const auto weak = base::make_weak(call.get());
	box->addButton(tr::lng_group_call_leave(), [=] {
		const auto discardCall = (discard && discard->checked());
		box->closeBox();

		if (!weak) {
			return;
		} else if (discardCall) {
			call->discard();
		} else {
			call->hangup();
		}
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void ConfirmBox(
		not_null<Ui::GenericBox*> box,
		const TextWithEntities &text,
		rpl::producer<QString> button,
		Fn<void()> callback) {
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box.get(),
			rpl::single(text),
			st::groupCallBoxLabel),
		st::boxPadding);
	if (callback) {
		box->addButton(std::move(button), callback);
	}
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void FillMenu(
		not_null<Ui::DropdownMenu*> menu,
		not_null<PeerData*> peer,
		not_null<GroupCall*> call,
		Fn<void()> chooseJoinAs,
		Fn<void(object_ptr<Ui::BoxContent>)> showBox) {
	const auto weak = base::make_weak(call.get());
	const auto resolveReal = [=] {
		const auto real = peer->groupCall();
		const auto strong = weak.get();
		return (real && strong && (real->id() == strong->id()))
			? real
			: nullptr;
	};
	const auto real = resolveReal();
	if (!real) {
		return;
	}

	const auto addEditJoinAs = call->showChooseJoinAs();
	const auto addEditTitle = peer->canManageGroupCall();
	const auto addEditRecording = peer->canManageGroupCall();
	if (addEditJoinAs) {
		menu->addAction(MakeJoinAsAction(
			menu->menu(),
			call->joinAs(),
			chooseJoinAs));
		menu->addSeparator();
	}
	if (addEditTitle) {
		menu->addAction(tr::lng_group_call_edit_title(tr::now), [=] {
			const auto done = [=](const QString &title) {
				if (const auto strong = weak.get()) {
					strong->changeTitle(title);
				}
			};
			if (const auto real = resolveReal()) {
				showBox(Box(
					EditGroupCallTitleBox,
					peer->name,
					real->title(),
					done));
			}
		});
	}
	if (addEditRecording) {
		const auto handler = [=] {
			const auto real = resolveReal();
			if (!real) {
				return;
			}
			const auto recordStartDate = real->recordStartDate();
			const auto done = [=](QString title) {
				if (const auto strong = weak.get()) {
					strong->toggleRecording(!recordStartDate, title);
				}
			};
			if (recordStartDate) {
				showBox(Box(
					StopGroupCallRecordingBox,
					done));
			} else {
				showBox(Box(
					StartGroupCallRecordingBox,
					real->title(),
					done));
			}
		};
		menu->addAction(MakeRecordingAction(
			menu->menu(),
			real->recordStartDateValue(),
			handler));
	}
	menu->addAction(tr::lng_group_call_settings(tr::now), [=] {
		if (const auto strong = weak.get()) {
			showBox(Box(SettingsBox, strong));
		}
	});
	menu->addAction(MakeFinishAction(menu->menu(), [=] {
		if (const auto strong = weak.get()) {
			showBox(Box(
				LeaveBox,
				strong,
				true,
				BoxContext::GroupCallPanel));
		}
	}));

}

} // namespace Calls::Group
