/*b=Bus.audio(s, 2);
SynthDef(\aa, {
	Out.ar(~engine_out, SinOsc.ar([330,440], 0, 0.4))
}).send(s)
a = Synth(\engine);
a={
	Out.ar(~engine_out, SinOsc.ar([330,440], 0, 0.4))
}.play //(s, b) //you won't hear this if you only have two channels
~engine_out.scope
{Out.ar(0, ~engine_out.ar)}.play(addAction:\addToTail)

{ Pulse.ar(90, 0.3, Amplitude.kr(SoundIn.ar(0))) }.play;

t={|attack=3,release=3|
	AmplitudeMod.kr(~engine_out.ar, attack, release)
}.scope(zoom:0.01);
{~control_gui2.value([
		[t, \attack, [0.5,5], "attack"],
		[t, \release, [0.5, 5], "release"],
	], false)}.defer(0.2);


Bus.audio(s,
{ SinOsc.ar(MouseX.kr(300, 15000, 1)) * 0.1 }.play;
(
{
    var freq;
    freq = MouseX.kr(300, 15000, 1);
    SinOsc.ar(freq) * 0.1 * AmpComp.kr(freq, 300)
}.play;
)
(200,210..10000).collect {|freq| (200/freq) ** 0.3333 }.plot;

(
// use input amplitude to control SinOsc frequency
{
    SinOsc.ar(
            Amplitude.kr(
                        Blip.ar(3, 20) * LFNoise1.kr(0.1).range(0, 1),
                        MouseX.kr(0.001, 1, 1),
                        MouseY.kr(0.001, 1, 1),
                        1200,
                        400
            ),
            0, 0.3)
}.play;
)
~test = Bus.audio(s, 2);
~k = {|v=1| Out.ar(~test, {v*SinOsc.ar(300)}!2)}.play
~amp_test = Bus.control(s)
{Out.kr(~amp_test, Amplitude.kr(~engine_out.ar, 0.5, 0.5, 1, 0))}.play
~amp_test.getSynchronous(s)
	~engine_out.scope()
{Amplitude.kr(~test.ar, 0.5, 0.5, 1, 0)}.scope
~k.free

{Amplitude.kr(Out.ar(0), 0.5, 0.5, 1200, 10)}.plot

~fedi_stream.get(\db, {|v| v.postln})
~fedi_stream.set(\db, -1)
~t1 = 10; ~t2 = 20;
"vol:"+(~t1+~t2).postln
~fedi_stream_vol_add.value(-1)
~fedi_stream.get(\db, {|v| ~fedi_stream.set(\db, v-1); ("vol:"+(v-1)).postln});
*/

(
// main output bus for volume control
~fedi_out = Bus.audio(s, 2);
// stream everything to output
~fedi_stream = { |db = 0.0|
	Out.ar(0, db.dbamp * ~fedi_out.ar);
}.play;
~fedi_stream_vol_add = {|x|
	~fedi_stream.get(\db, {|v| ~fedi_stream.set(\db, v+x); ("vol:"+(v+x)).postln});
};
OSCdef(\msg_vol_up, {|m| ~fedi_stream_vol_add.value(0.2)}, '/vol_up');
OSCdef(\msg_vol_down, {|m| ~fedi_stream_vol_add.value(-0.2)}, '/vol_down');

// misc functions
~free_synth = {|s| if(s !== nil, {s.free}); nil};
~close_window = {|s| if(s !== nil, {{s.close}.defer(0.2)}); nil};
~show_controls_osc_none = {
	OSCdef(\msg_show_controls, {|m|}, '/show_controls');
};


// generate a slurp-sound when above optimal rpm
~buf_slurp = Buffer.read(s, "/Users/jhammers/slurp-sounds/slurp_cont.wav");
~bus_slurp = Bus.audio(s, 2);

~slurp_play = {|loop=1, startPos=0| // startPos in seconds
	~slurp = {
		Out.ar(~bus_slurp,
			PlayBuf.ar(2, ~buf_slurp, BufRateScale.kr(~buf_slurp), // * MouseY.kr(0.5,1.5),
			startPos: BufSampleRate.kr(~buf_slurp) * startPos,
			loop: loop));
	}.play;
};
~slurp_lpf_play = {
	~slurp_lpf = {|f=15000,m=2| // f:[200,15000], m:[0,5] (oder so ..)
		Out.ar(~fedi_out, LPF.ar(~bus_slurp.ar, f, m)) //*1.5)
	}.play;
};
~slurp_stop = {
	~set_rpm_handler.value;
	if (~slurp != nil, {~slurp.free; ~slurp = nil});
	if (~slurp_lpf != nil, {~slurp_lpf.free; ~slurp_lpf = nil});
};
~slurp_start = {
	~slurp_stop.value;
	~slurp_lpf_play.value;
	OSCdef(\msg_rpm, {|m|
		~rpm_handler.value(m[1]); // still control the engine-sound!
		if (~gear == 6, {~slurp_lpf.set(\m, 0)},{~slurp_lpf.set(\m, m[1].linlin(0.38,0.65, 0,1.2));});
		//~lpf.set(\f, m[1].linlin(0.38,1, 400,15000));
		//m[1].postln;
	}, '/rpm');
	{~slurp_play.value}.defer(0.2);
};
OSCdef(\msg_slurp_start, {|m|
	"slurp_start".postln;
	~slurp_start.value;
}, '/slurp_start');
OSCdef(\msg_slurp_stop, {|m|
	"slurp_stop".postln;
	~slurp_stop.value;
}, '/slurp_stop');


// map fuel consumption on pitched noise
~pitch_stop = {
	~pitch = ~free_synth.value(~pitch);
	~pitch_control_window = ~close_window.value(~pitch_control_window);
	~show_controls_osc_none.value;
};
~pitch_show_controls = {
	{~pitch_control_window = ~control_gui2.value([
		[~pitch, \f, [300,3000], "freq"],
		[~pitch, \bw, [0.01, 0.6], "bandwidth"],
		[~pitch, \a, [0.4,5], "ampl."],
	], false)}.defer(0.2);
};
~pitch_show_controls_osc = {
	OSCdef(\msg_show_controls, {|m|
		~pitch_show_controls.value;
	}, '/show_controls');
};
OSCdef(\msg_pitch_stop, {|m|
	"pitch_stop".postln;
	~pitch_stop.value;
}, '/pitch_stop');
OSCdef(\msg_pitch_start, {|m|
	"pitch_start".postln;
	~pitch_stop.value;
	~pitch = {|f=300,bw=0.1,a=1| Out.ar(~fedi_out, {a*Resonz.ar(WhiteNoise.ar(0.5), f, bw)}!2)}.play;
	OSCdef(\msg_L_100km, {|m| // range [0-40?]
		if(m[1].isNaN.not && (m[1] != inf) && (~pitch != nil), {
			~pitch.set(\f, m[1].linlin(0,40,300,3000));
		});
	}, '/L_100km');
	~pitch_show_controls_osc.value;
}, '/pitch_start');


// map (absolute) fuel consumption on the frequency of grains
// optionally with modifying the cutoff freq based on l/100km
// here, the impulses for the grains come directly from the program

~grain_pitch = false; // modify cutoff freq based on l/100km
~sound_control_bus = Bus.control(s, 3); // control bus for controlling the triggered sound
~triggered_sound = { // the sound that gets triggered
	var p = ~sound_control_bus.getnSynchronous(3);
	Out.ar(~fedi_out, {LPF.ar(WhiteNoise.ar(p[2]), p[0])
		* EnvGen.ar(Env.perc(0.001,0.5), doneAction:2)}!2);
};
~grain_stop = {
	~sound_control = ~free_synth.value(~sound_control);
	OSCdef(\msg_consumption_tick, {|m|}, '/consumption_tick'); // no response!
	~grain_control_window = ~close_window.value(~grain_control_window);
	~show_controls_osc_none.value;
};
~grain_show_controls = {
	{~grain_control_window = ~control_gui2.value([
		[~onset_input, \f, [0,3], "freq"],
		[~sound_control, \f, [200, 2000], "PF freq"],
		[~sound_control, \bw, [0.01, 0.6], "BPF bandwidth"],
		[~sound_control, \a, [0.4,5], "amplitude"],
	], false)}.defer(0.2);
};
OSCdef(\msg_grain_start, {|m|
	"grain_start".postln;
	~grain_stop.value;
	~grain_pitch = false;
	~sound_control = {|f=300, bw=0.1, a=1| // synth writing on the control bus
		Out.kr(~sound_control_bus, [f, bw, a])
	}.play;
	OSCdef(\msg_consumption_tick, {|m|
		//~freq = m[1]; ...
		~triggered_sound.play() //args: [\f, ~freq])
	}, '/consumption_tick');
	OSCdef(\msg_show_controls, {|m|
		~grain_show_controls.value;
	}, '/show_controls');
}, '/grain_start');
OSCdef(\msg_grain_stop, {|m|
	"grain_stop".postln;
	~grain_stop.value;
}, '/grain_stop');
OSCdef(\msg_grain_toggle_pitch, {|m|
	if(~grain_pitch, {
		"grain_pitch: off".postln;
		OSCdef(\msg_L_100km, {|m|}, '/L_100km');
		~grain_pitch = false;
	}, {
		"grain_pitch: on".postln;
		OSCdef(\msg_L_100km, {|m| // range [0-40?] // this is optional!
			if(m[1].isNaN.not && (m[1] != inf), {
				~sound_control.set(\f, m[1].linlin(0,40,300,2000));
			});
		}, '/L_100km');
		~grain_pitch = true;
	});
}, '/grain_toggle_pitch');


"FEDIs initialized!"
)


(

)





// map (absolute) fuel consumption on the frequency of grains
// optionally with modifying the cutoff freq based on l/100km
(
~onset_input_bus = Bus.audio(s); // input for the impulses
~onset_bus = Bus.control(s); // control bus receiving the trigger
~onset_trigger = { // convert data from the control-bus to trigger signals
	SendTrig.kr(~onset_bus.kr);
}.play;
~onset_detect = { |threshold = 0.2, floor = 0.15| // impulses => trigger control bus
	var d = Buffer.alloc(s, 512);
	var detect = Onsets.kr(FFT(d, ~onset_input_bus.ar), threshold, mingap: 0.1, floor: floor);
	Out.kr(~onset_bus, detect);
}.play;
~sound_control_bus = Bus.control(s, 3); // control bus for controlling the triggered sound
~sound_control = {|f=300, bw=0.1, a=1| // synth writing on the control bus
	Out.kr(~sound_control_bus, [f, bw, a])
}.play;
~triggered_sound = { // the sound that gets triggered
	var p = ~sound_control_bus.getnSynchronous(3);
	{LPF.ar(WhiteNoise.ar(p[2]), p[0])
		* EnvGen.ar(Env.perc(0.001,0.5), doneAction:2)}!2;
};
/*~triggered_sound2 = {
	var sc = ~sound_control_bus.getnSynchronous(3);
	Resonz.ar(WhiteNoise.ar(sc[2]), sc[0], sc[1])
	* EnvGen.ar(Env.perc(0.001,0.5), doneAction:2);
};*/
~onset_input = {|f=2| // generating the impulses
	Out.ar(~onset_input_bus, Impulse.ar(f));
}.play;
~trigger_func.free;
~trigger_func = OSCFunc({~triggered_sound.play},'/tr', s.addr);
{~control_gui2.value([
	[~onset_input, \f, [0,3], "freq"],
	[~sound_control, \f, [200, 2000], "PF freq"],
	[~sound_control, \bw, [0.01, 0.6], "BPF bandwidth"],
	[~sound_control, \a, [0.4,5], "amplitude"],
], false)}.defer(0.2);

OSCdef(\msg_ml_sec, {|m| // range: [0-5]
	~onset_input.set(\f, m[1].linlin(0,5,0,1.5));
}, '/ml_sec');

OSCdef(\msg_L_100km, {|m| // range [0-40?] // this is optional!
	if(m[1].isNaN.not && (m[1] != inf), {
		~sound_control.set(\f, m[1].linlin(0,40,300,2000));
	});
}, '/L_100km');
)


(
//only testing (slurp)
{LPF.ar(~bus_slurp.ar,
	MouseX.kr(200, 8000),
	MouseX.kr(0,5)
)}.play;
//{~bus_slurp.ar}.play;
/*	{
		~control_gui2.value([[~lpf, \f, [200,10000], "frequency"],
							//[~lpf, \m, [-20,15], "mul"]
							[~lpf, \m, [0,5], "mul"]
						], false);
	}.defer(0.2);*/
~play.value;

)
//~slurp.free;

/////// TESTING /////////

// (
// n=LFNoise1;
// Ndef(\x,{|f=65|
// 	a=SinOsc.ar(f, Ndef(\x).ar*n.ar(0.1,3), n.ar(3,6)).tanh;
// 	9.do{
// 		a=AllpassL.ar(a,0.3,{0.2.rand+0.1}!2,5)
// 	};
// 	a.tanh
// }).play
// )
//
// (
// n=LFNoise1;
// Ndef(\x,{|f=100|
// 	a=SinOsc.ar(f, Ndef(\x).ar*n.ar(0.1,3), n.ar(3,1)).tanh;
// 	9.do{
// 		a=AllpassL.ar(a,0.3,{0.2.rand+0.1}!2,5)
// 	};
// 	a.tanh
// }).play;
// w = Ndef(\x);
// )
//
//
// play{a=HPF.ar(ar(PinkNoise,5e-3),10)*Line.kr(0,1,9);ar(GVerb,({|i|ar(Ringz,a*LFNoise1.kr(0.05+0.1.rand),55*i+60,0.2)}!99).sum,70,99).tanh}

//{ Klank.ar(`[[440, 980, 1220, 1560], nil, [2, 2, 2, 2]], WhiteNoise.ar(0.005)) }.play;
// (
// fork{
// 	100.do({
// 		{
// 			Klank.ar(`[[440, 980, 1220, 1560], nil, [2, 2, 1, 1]], WhiteNoise.ar(0.005))
// 			//LPF.ar(WhiteNoise.ar(1), MouseX.kr(200,20000, 1))
// 			* EnvGen.ar(Env.perc(0.01,1), doneAction:2)
// 		}.play;
// 		1.wait;
// 	});
// }
//)