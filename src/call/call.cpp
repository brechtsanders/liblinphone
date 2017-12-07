/*
 * call.cpp
 * Copyright (C) 2010-2017 Belledonne Communications SARL
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "c-wrapper/c-wrapper.h"
#include "call-p.h"
#include "conference/session/call-session-p.h"
#include "conference/session/media-session-p.h"
#include "core/core-p.h"
#include "logger/logger.h"

// =============================================================================

using namespace std;

LINPHONE_BEGIN_NAMESPACE

bool CallPrivate::getAudioMuted () const {
	return static_pointer_cast<MediaSession>(getActiveSession())->getPrivate()->getAudioMuted();
}

LinphoneProxyConfig *CallPrivate::getDestProxy () const {
	return getActiveSession()->getPrivate()->getDestProxy();
}

IceSession *CallPrivate::getIceSession () const {
	return static_pointer_cast<MediaSession>(getActiveSession())->getPrivate()->getIceSession();
}

unsigned int CallPrivate::getMediaStartCount () const {
	return static_pointer_cast<MediaSession>(getActiveSession())->getPrivate()->getMediaStartCount();
}

MediaStream *CallPrivate::getMediaStream (LinphoneStreamType type) const {
	return static_pointer_cast<MediaSession>(getActiveSession())->getPrivate()->getMediaStream(type);
}

SalCallOp * CallPrivate::getOp () const {
	return getActiveSession()->getPrivate()->getOp();
}

void CallPrivate::setAudioMuted (bool value) {
	static_pointer_cast<MediaSession>(getActiveSession())->getPrivate()->setAudioMuted(value);
}

// -----------------------------------------------------------------------------

void CallPrivate::initiateIncoming () {
	getActiveSession()->initiateIncoming();
}

bool CallPrivate::initiateOutgoing () {
	return getActiveSession()->initiateOutgoing();
}

void CallPrivate::iterate (time_t currentRealTime, bool oneSecondElapsed) {
	getActiveSession()->iterate(currentRealTime, oneSecondElapsed);
}

void CallPrivate::startIncomingNotification () {
	getActiveSession()->startIncomingNotification();
}

int CallPrivate::startInvite (const Address *destination) {
	return getActiveSession()->startInvite(destination, "");
}

// -----------------------------------------------------------------------------

void CallPrivate::createPlayer () const {
	L_Q();
	player = linphone_call_build_player(L_GET_C_BACK_PTR(q));
}

// -----------------------------------------------------------------------------

void CallPrivate::startRemoteRing () {
	L_Q();
	LinphoneCore *lc = q->getCore()->getCCore();
	if (!lc->sound_conf.play_sndcard)
		return;

	MSSndCard *ringCard = lc->sound_conf.lsd_card ? lc->sound_conf.lsd_card : lc->sound_conf.play_sndcard;
	int maxRate = static_pointer_cast<MediaSession>(getActiveSession())->getPrivate()->getLocalDesc()->streams[0].max_rate;
	if (maxRate > 0)
		ms_snd_card_set_preferred_sample_rate(ringCard, maxRate);
	// We release sound before playing ringback tone
	AudioStream *as = reinterpret_cast<AudioStream *>(getMediaStream(LinphoneStreamTypeAudio));
	if (as)
		audio_stream_unprepare_sound(as);
	if (lc->sound_conf.remote_ring) {
		lc->ringstream = ring_start(lc->factory, lc->sound_conf.remote_ring, 2000, ringCard);
	}
}

void CallPrivate::terminateBecauseOfLostMedia () {
	L_Q();
	lInfo() << "Call [" << q << "]: Media connectivity with " << q->getRemoteAddressAsString()
		<< " is lost, call is going to be terminated";
	static_pointer_cast<MediaSession>(getActiveSession())->terminateBecauseOfLostMedia();
	linphone_core_play_named_tone(q->getCore()->getCCore(), LinphoneToneCallLost);
}

// -----------------------------------------------------------------------------

void CallPrivate::onAckBeingSent (LinphoneHeaders *headers) {
	L_Q();
	linphone_call_notify_ack_processing(L_GET_C_BACK_PTR(q), headers, false);
}

void CallPrivate::onAckReceived (LinphoneHeaders *headers) {
	L_Q();
	linphone_call_notify_ack_processing(L_GET_C_BACK_PTR(q), headers, true);
}

void CallPrivate::onBackgroundTaskToBeStarted () {
	backgroundTaskId = sal_begin_background_task("liblinphone call notification", nullptr, nullptr);
}

void CallPrivate::onBackgroundTaskToBeStopped () {
	if (backgroundTaskId != 0) {
		sal_end_background_task(backgroundTaskId);
		backgroundTaskId = 0;
	}
}

bool CallPrivate::onCallAccepted () {
	L_Q();
	LinphoneCore *lc = q->getCore()->getCCore();
	bool wasRinging = false;

	if (q->getCore()->getCurrentCall() != q->getSharedFromThis())
		linphone_core_preempt_sound_resources(lc);

	// Stop ringing
	if (linphone_ringtoneplayer_is_started(lc->ringtoneplayer)) {
		lInfo() << "Stop ringing";
		linphone_core_stop_ringing(lc);
		wasRinging = true;
	}
	if (ringingBeep) {
		linphone_core_stop_dtmf(lc);
		ringingBeep = false;
	}
	return wasRinging;
}

void CallPrivate::onCallSetReleased () {
	L_Q();
	linphone_call_unref(L_GET_C_BACK_PTR(q));
}

void CallPrivate::onCallSetTerminated () {
	L_Q();
	LinphoneCore *core = q->getCore()->getCCore();
	if (q->getSharedFromThis() == q->getCore()->getCurrentCall()) {
		lInfo() << "Resetting the current call";
		q->getCore()->getPrivate()->setCurrentCall(nullptr);
	}
	if (q->getCore()->getPrivate()->removeCall(q->getSharedFromThis()) != 0)
		lError() << "Could not remove the call from the list!!!";
#if 0
	if (core->conf_ctx)
		linphone_conference_on_call_terminating(core->conf_ctx, lcall);
#endif
	if (ringingBeep) {
		linphone_core_stop_dtmf(core);
		ringingBeep = false;
	}
#if 0
	if (lcall->chat_room)
		linphone_chat_room_set_call(lcall->chat_room, nullptr);
#endif // if 0
	if (!q->getCore()->getPrivate()->hasCalls())
		ms_bandwidth_controller_reset_state(core->bw_controller);
}

void CallPrivate::onCallStateChanged (LinphoneCallState state, const string &message) {
	L_Q();
	linphone_call_notify_state_changed(L_GET_C_BACK_PTR(q), state, message.c_str());
}

void CallPrivate::onCheckForAcceptation () {
	L_Q();
	LinphoneCall *lcall = L_GET_C_BACK_PTR(q);
	bctbx_list_t *copy = bctbx_list_copy(linphone_core_get_calls(q->getCore()->getCCore()));
	for (bctbx_list_t *it = copy; it != nullptr; it = bctbx_list_next(it)) {
		LinphoneCall *call = reinterpret_cast<LinphoneCall *>(bctbx_list_get_data(it));
		if (call == lcall) continue;
		switch (linphone_call_get_state(call)) {
			case LinphoneCallOutgoingInit:
			case LinphoneCallOutgoingProgress:
			case LinphoneCallOutgoingRinging:
			case LinphoneCallOutgoingEarlyMedia:
				lInfo() << "Already existing call [" << call << "] in state [" << linphone_call_state_to_string(linphone_call_get_state(call)) <<
					"], canceling it before accepting new call [" << lcall << "]";
				linphone_call_terminate(call);
				break;
			default:
				break; /* Nothing to do */
		}
	}
	bctbx_list_free(copy);
}

void CallPrivate::onDtmfReceived (char dtmf) {
	L_Q();
	linphone_call_notify_dtmf_received(L_GET_C_BACK_PTR(q), dtmf);
}

void CallPrivate::onIncomingCallNotified () {
	L_Q();
	/* The call is acceptable so we can now add it to our list */
	q->getCore()->getPrivate()->addCall(q->getSharedFromThis());
}

void CallPrivate::onIncomingCallStarted () {
	L_Q();
	linphone_core_notify_incoming_call(q->getCore()->getCCore(), L_GET_C_BACK_PTR(q));
}

void CallPrivate::onIncomingCallTimeoutCheck (int elapsed, bool oneSecondElapsed) {
	L_Q();
	if (oneSecondElapsed)
		lInfo() << "Incoming call ringing for " << elapsed << " seconds";
	if (elapsed > q->getCore()->getCCore()->sip_conf.inc_timeout) {
		lInfo() << "Incoming call timeout (" << q->getCore()->getCCore()->sip_conf.inc_timeout << ")";
		LinphoneReason declineReason = (q->getCore()->getCurrentCall() != q->getSharedFromThis())
			? LinphoneReasonBusy : LinphoneReasonDeclined;
		getActiveSession()->declineNotAnswered(declineReason);
	}
}

void CallPrivate::onInfoReceived (const LinphoneInfoMessage *im) {
	L_Q();
	linphone_call_notify_info_message_received(L_GET_C_BACK_PTR(q), im);
}

void CallPrivate::onNoMediaTimeoutCheck (bool oneSecondElapsed) {
	L_Q();
	int disconnectTimeout = linphone_core_get_nortp_timeout(q->getCore()->getCCore());
	bool disconnected = false;
	AudioStream *as = reinterpret_cast<AudioStream *>(getMediaStream(LinphoneStreamTypeAudio));
	if (((q->getState() == LinphoneCallStreamsRunning) || (q->getState() == LinphoneCallPausedByRemote))
		&& oneSecondElapsed && as && (as->ms.state == MSStreamStarted) && (disconnectTimeout > 0))
		disconnected = !audio_stream_alive(as, disconnectTimeout);
	if (disconnected)
		terminateBecauseOfLostMedia();
}

void CallPrivate::onEncryptionChanged (bool activated, const string &authToken) {
	L_Q();
	linphone_call_notify_encryption_changed(L_GET_C_BACK_PTR(q), activated, authToken.empty() ? nullptr : authToken.c_str());
}

void CallPrivate::onStatsUpdated (const LinphoneCallStats *stats) {
	L_Q();
	linphone_call_notify_stats_updated(L_GET_C_BACK_PTR(q), stats);
}

void CallPrivate::onResetCurrentCall () {
	L_Q();
	q->getCore()->getPrivate()->setCurrentCall(nullptr);
}

void CallPrivate::onSetCurrentCall () {
	L_Q();
	q->getCore()->getPrivate()->setCurrentCall(q->getSharedFromThis());
}

void CallPrivate::onFirstVideoFrameDecoded () {
	L_Q();
	if (nextVideoFrameDecoded._func) {
		nextVideoFrameDecoded._func(L_GET_C_BACK_PTR(q), nextVideoFrameDecoded._user_data);
		nextVideoFrameDecoded._func = nullptr;
		nextVideoFrameDecoded._user_data = nullptr;
	}
}

void CallPrivate::onResetFirstVideoFrameDecoded () {
#ifdef VIDEO_ENABLED
	if (nextVideoFrameDecoded._func)
		static_cast<MediaSession *>(getActiveSession().get())->resetFirstVideoFrameDecoded();
#endif // ifdef VIDEO_ENABLED
}

void CallPrivate::onPlayErrorTone (LinphoneReason reason) {
	L_Q();
	linphone_core_play_call_error_tone(q->getCore()->getCCore(), reason);
}

void CallPrivate::onRingbackToneRequested (bool requested) {
	L_Q();
	if (requested && linphone_core_get_remote_ringback_tone(q->getCore()->getCCore()))
		playingRingbackTone = true;
	else if (!requested)
		playingRingbackTone = false;
}

void CallPrivate::onStartRinging () {
	L_Q();
	LinphoneCore *lc = q->getCore()->getCCore();
	if (lc->ringstream)
		return; // Already ringing!
	startRemoteRing();
}

void CallPrivate::onStopRinging () {
	L_Q();
	linphone_core_stop_ringing(q->getCore()->getCCore());
}

void CallPrivate::onStopRingingIfInCall () {
	L_Q();
	LinphoneCore *lc = q->getCore()->getCCore();
	// We stop the ring only if we have this current call or if we are in call
	if ((q->getCore()->getCallCount() == 1) || linphone_core_in_call(lc)) {
		linphone_core_stop_ringing(lc);
	}
}

void CallPrivate::onStopRingingIfNeeded () {
	L_Q();
	LinphoneCore *lc = q->getCore()->getCCore();
	bool stopRinging = true;
	bool ringDuringEarlyMedia = linphone_core_get_ring_during_incoming_early_media(lc);
	for (const auto &call : q->getCore()->getCalls()) {
		if ((call->getState() == LinphoneCallIncomingReceived)
			|| (ringDuringEarlyMedia && call->getState() == LinphoneCallIncomingEarlyMedia)) {
			stopRinging = false;
			break;
		}
	}
	if (stopRinging)
		linphone_core_stop_ringing(lc);
}

bool CallPrivate::isPlayingRingbackTone () {
	return playingRingbackTone;
}

// =============================================================================

Call::Call (CallPrivate &p, shared_ptr<Core> core) : Object(p), CoreAccessor(core) {
	L_D();
	d->nextVideoFrameDecoded._func = nullptr;
	d->nextVideoFrameDecoded._user_data = nullptr;
}

// -----------------------------------------------------------------------------

LinphoneStatus Call::accept (const MediaSessionParams *msp) {
	L_D();
	return static_pointer_cast<MediaSession>(d->getActiveSession())->accept(msp);
}

LinphoneStatus Call::acceptEarlyMedia (const MediaSessionParams *msp) {
	L_D();
	return static_pointer_cast<MediaSession>(d->getActiveSession())->acceptEarlyMedia(msp);
}

LinphoneStatus Call::acceptUpdate (const MediaSessionParams *msp) {
	L_D();
	return static_pointer_cast<MediaSession>(d->getActiveSession())->acceptUpdate(msp);
}

void Call::cancelDtmfs () {
	L_D();
	static_pointer_cast<MediaSession>(d->getActiveSession())->cancelDtmfs();
}

LinphoneStatus Call::decline (LinphoneReason reason) {
	L_D();
	return d->getActiveSession()->decline(reason);
}

LinphoneStatus Call::decline (const LinphoneErrorInfo *ei) {
	L_D();
	return d->getActiveSession()->decline(ei);
}

LinphoneStatus Call::deferUpdate () {
	L_D();
	return d->getActiveSession()->deferUpdate();
}

void Call::oglRender () const {
	L_D();
	static_pointer_cast<MediaSession>(d->getActiveSession())->getPrivate()->oglRender();
}

LinphoneStatus Call::pause () {
	L_D();
	return static_pointer_cast<MediaSession>(d->getActiveSession())->pause();
}

LinphoneStatus Call::redirect (const std::string &redirectUri) {
	L_D();
	return d->getActiveSession()->redirect(redirectUri);
}

LinphoneStatus Call::resume () {
	L_D();
	return static_pointer_cast<MediaSession>(d->getActiveSession())->resume();
}

LinphoneStatus Call::sendDtmf (char dtmf) {
	L_D();
	return static_pointer_cast<MediaSession>(d->getActiveSession())->sendDtmf(dtmf);
}

LinphoneStatus Call::sendDtmfs (const std::string &dtmfs) {
	L_D();
	return static_pointer_cast<MediaSession>(d->getActiveSession())->sendDtmfs(dtmfs);
}

void Call::sendVfuRequest () {
	L_D();
	static_pointer_cast<MediaSession>(d->getActiveSession())->sendVfuRequest();
}

void Call::startRecording () {
	L_D();
	static_pointer_cast<MediaSession>(d->getActiveSession())->startRecording();
}

void Call::stopRecording () {
	L_D();
	static_pointer_cast<MediaSession>(d->getActiveSession())->stopRecording();
}

LinphoneStatus Call::takePreviewSnapshot (const string &file) {
	L_D();
	return static_pointer_cast<MediaSession>(d->getActiveSession())->takePreviewSnapshot(file);
}

LinphoneStatus Call::takeVideoSnapshot (const string &file) {
	L_D();
	return static_pointer_cast<MediaSession>(d->getActiveSession())->takeVideoSnapshot(file);
}

LinphoneStatus Call::terminate (const LinphoneErrorInfo *ei) {
	L_D();
	return d->getActiveSession()->terminate(ei);
}

LinphoneStatus Call::update (const MediaSessionParams *msp) {
	L_D();
	return static_pointer_cast<MediaSession>(d->getActiveSession())->update(msp);
}

void Call::zoomVideo (float zoomFactor, float *cx, float *cy) {
	zoomVideo(zoomFactor, *cx, *cy);
}

void Call::zoomVideo (float zoomFactor, float cx, float cy) {
	L_D();
	static_pointer_cast<MediaSession>(d->getActiveSession())->zoomVideo(zoomFactor, cx, cy);
}

// -----------------------------------------------------------------------------

bool Call::cameraEnabled () const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->cameraEnabled();
}

bool Call::echoCancellationEnabled () const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->echoCancellationEnabled();
}

bool Call::echoLimiterEnabled () const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->echoLimiterEnabled();
}

void Call::enableCamera (bool value) {
	L_D();
	static_pointer_cast<MediaSession>(d->getActiveSession())->enableCamera(value);
}

void Call::enableEchoCancellation (bool value) {
	L_D();
	static_pointer_cast<MediaSession>(d->getActiveSession())->enableEchoCancellation(value);
}

void Call::enableEchoLimiter (bool value) {
	L_D();
	static_pointer_cast<MediaSession>(d->getActiveSession())->enableEchoLimiter(value);
}

bool Call::getAllMuted () const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->getAllMuted();
}

LinphoneCallStats *Call::getAudioStats () const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->getAudioStats();
}

string Call::getAuthenticationToken () const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->getAuthenticationToken();
}

bool Call::getAuthenticationTokenVerified () const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->getAuthenticationTokenVerified();
}

float Call::getAverageQuality () const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->getAverageQuality();
}

const MediaSessionParams *Call::getCurrentParams () const {
	L_D();
	return static_pointer_cast<MediaSession>(d->getActiveSession())->getCurrentParams();
}

float Call::getCurrentQuality () const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->getCurrentQuality();
}

LinphoneCallDir Call::getDirection () const {
	L_D();
	return d->getActiveSession()->getDirection();
}

const Address &Call::getDiversionAddress () const {
	L_D();
	return d->getActiveSession()->getDiversionAddress();
}

int Call::getDuration () const {
	L_D();
	return d->getActiveSession()->getDuration();
}

const LinphoneErrorInfo *Call::getErrorInfo () const {
	L_D();
	return d->getActiveSession()->getErrorInfo();
}

LinphoneCallLog *Call::getLog () const {
	L_D();
	return d->getActiveSession()->getLog();
}

RtpTransport *Call::getMetaRtcpTransport (int streamIndex) const {
	L_D();
	return static_pointer_cast<MediaSession>(d->getActiveSession())->getMetaRtcpTransport(streamIndex);
}

RtpTransport *Call::getMetaRtpTransport (int streamIndex) const {
	L_D();
	return static_pointer_cast<MediaSession>(d->getActiveSession())->getMetaRtpTransport(streamIndex);
}

float Call::getMicrophoneVolumeGain () const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->getMicrophoneVolumeGain();
}

void *Call::getNativeVideoWindowId () const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->getNativeVideoWindowId();
}

const MediaSessionParams *Call::getParams () const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->getMediaParams();
}

LinphonePlayer *Call::getPlayer () const {
	L_D();
	if (!d->player)
		d->createPlayer();
	return d->player;
}

float Call::getPlayVolume () const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->getPlayVolume();
}

LinphoneReason Call::getReason () const {
	L_D();
	return d->getActiveSession()->getReason();
}

float Call::getRecordVolume () const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->getRecordVolume();
}

const Address &Call::getRemoteAddress () const {
	L_D();
	return d->getActiveSession()->getRemoteAddress();
}

string Call::getRemoteAddressAsString () const {
	L_D();
	return d->getActiveSession()->getRemoteAddressAsString();
}

string Call::getRemoteContact () const {
	L_D();
	return d->getActiveSession()->getRemoteContact();
}

const MediaSessionParams *Call::getRemoteParams () const {
	L_D();
	return static_pointer_cast<MediaSession>(d->getActiveSession())->getRemoteParams();
}

string Call::getRemoteUserAgent () const {
	L_D();
	return d->getActiveSession()->getRemoteUserAgent();
}

float Call::getSpeakerVolumeGain () const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->getSpeakerVolumeGain();
}

LinphoneCallState Call::getState () const {
	L_D();
	return d->getActiveSession()->getState();
}

LinphoneCallStats *Call::getStats (LinphoneStreamType type) const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->getStats(type);
}

int Call::getStreamCount () const {
	L_D();
	return static_pointer_cast<MediaSession>(d->getActiveSession())->getStreamCount();
}

MSFormatType Call::getStreamType (int streamIndex) const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->getStreamType(streamIndex);
}

LinphoneCallStats *Call::getTextStats () const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->getTextStats();
}

const Address &Call::getToAddress () const {
	L_D();
	return d->getActiveSession()->getToAddress();
}

string Call::getToHeader (const std::string &name) const {
	L_D();
	return d->getActiveSession()->getToHeader(name);
}

LinphoneCallStats *Call::getVideoStats () const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->getVideoStats();
}

bool Call::mediaInProgress () const {
	L_D();
	return static_pointer_cast<const MediaSession>(d->getActiveSession())->mediaInProgress();
}

void Call::setAudioRoute (LinphoneAudioRoute route) {
	L_D();
	static_pointer_cast<MediaSession>(d->getActiveSession())->setAudioRoute(route);
}

void Call::setAuthenticationTokenVerified (bool value) {
	L_D();
	static_pointer_cast<MediaSession>(d->getActiveSession())->setAuthenticationTokenVerified(value);
}

void Call::setMicrophoneVolumeGain (float value) {
	L_D();
	static_pointer_cast<MediaSession>(d->getActiveSession())->setMicrophoneVolumeGain(value);
}

void Call::setNativeVideoWindowId (void *id) {
	L_D();
	static_pointer_cast<MediaSession>(d->getActiveSession())->setNativeVideoWindowId(id);
}

void Call::setNextVideoFrameDecodedCallback (LinphoneCallCbFunc cb, void *user_data) {
	L_D();
	d->nextVideoFrameDecoded._func = cb;
	d->nextVideoFrameDecoded._user_data = user_data;
	d->onResetFirstVideoFrameDecoded();
}

void Call::setParams (const MediaSessionParams *msp) {
	L_D();
	static_pointer_cast<MediaSession>(d->getActiveSession())->setParams(msp);
}

void Call::setSpeakerVolumeGain (float value) {
	L_D();
	static_pointer_cast<MediaSession>(d->getActiveSession())->setSpeakerVolumeGain(value);
}

LINPHONE_END_NAMESPACE
