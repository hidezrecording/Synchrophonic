# Profound Listening Experiences — Research Report for SG-9 Design

**Compiled:** 2026-10-03. Purpose: drive DSP and UX design decisions for the SG-9 solfeggio drone instrument.
Goal: reliably produce awe, chills, ego dissolution, transformative states — not just relaxation.

---

## 1. Musical frisson (chills): neuroscience and triggering features

### Core findings
- **Blood & Zatorre (2001, PNAS):** intensely pleasurable musical moments ("chills") correlate with activity in reward/emotion brain regions (ventral striatum, amygdala).
  https://www.news-medical.net/news/20110113/The-neuroscience-of-musical-chills.aspx
- **Salimpoor et al. (2011, Nature Neuroscience):** dopamine is released in the dorsal striatum (caudate) during *anticipation* of the peak and in the ventral striatum (nucleus accumbens) during the peak experience itself. Anticipation and climax are anatomically distinct.
  https://www.newswise.com/articles/the-rewarding-aspects-of-music-listening-involve-the-dopaminergic-striatal-reward
- **Ferreri et al. (2017, PNAS):** enhancing dopamine (levodopa) increased musical pleasure; inhibiting it (risperidone) decreased it — **causal pharmacological evidence** that dopamine gates music-evoked pleasure, not just correlation.
- **Musical features that trigger frisson** (Harrison & Loui review of the literature, incl. Sloboda 1991, Huron 2006, Guhn/Hamm/Zentner 2007, Panksepp 1995):
  - Onsets of frisson most likely during **peaks in loudness, moments of modulation (key/harmonic change), and when melody occupies the human vocal register**.
  - **Tears** most likely elicited by **appoggiaturas**; **shivers/gooseflesh** by **onset of new harmonies**.
  - **Sudden dynamic leaps (soft → loud)** are the most replicated catalyst across studies; moves to extreme softness occasionally work too.
  - Huron: acoustic correlates include **rapidly large increases in loudness, abrupt tempo/rhythm changes, broadening of frequencies, increase in number of sound sources** — all "low-probability musical events" that surprise the nervous system. He theorizes frisson = fear-circuitry (amygdala) that gets cortically reappraised as safe → pleasure.
  http://mindlab.research.wesleyan.edu/files/2014/01/102028_Loui_Manuscript.pdf
  https://phys.org/news/2008-05-scholar-explores-mystery-music-evoked-frisson.pdf
- **Predictability vs. novelty:** Loui: "if a song follows the conventions too closely, it is bland... if it breaks the patterns too much, it sounds like noise. But when composers straddle the boundary between the familiar and unfamiliar, playing with your expectations using unpredictable flourishes (like appoggiaturas or sweeping harmonic changes), they hit a sweet spot." This maps onto the **Wundt curve (inverted-U)**: liking peaks at *intermediate* novelty/complexity — neither fully predictable nor chaotic. Demonstrated in music repeatedly (meta-discussion: https://www.marcus-pearce.com/assets/papers/ZiogaEtAl2024.pdf ; liking/predictability: https://www.nature.com/articles/s41598-019-53510-w). **Crucially, the sweet spot varies between individuals** (Lisøy et al. 2022, PLOS ONE: https://journals.plos.org/plosone/article/file?id=10.1371/journal.pone.0275308&type=printable).
- **Individual differences:** ~2/3 of people experience frisson; high **openness to experience** predicts more frisson (Sachs et al. 2016 — stronger auditory↔reward-network connectivity in chill-prone listeners). https://universityobserver.ie/music-on-the-brain/

### Honest caveats
- Dopamine PET studies are small-N, use self-selected favorite music (maximal-response design) — effect sizes for generic music are smaller.
- Feature lists are largely correlational/retrospective (what people *report* about pieces that gave them chills), not from controlled parametric experiments.

### Actionable takeaways for SG-9
1. **Engineer "chill moments" into the session arc:** slow crescendos (soft→loud dynamic swells), harmonic arrivals (a new harmony entering over the drone — the #1 shiver trigger), neighbor-tone/appoggiatura-like resolutions onto chakra fundamentals (the tear trigger). These should be *composed events*, not random.
2. **Keep the drone bed hyper-predictable** (Wundt logic: familiar foundation) and place **intermediate-novelty events** on top: e.g., harmonics that emerge, a voice partial that enters a fifth above, a sudden swell. Too much novelty = noise; too little = bland.
3. **Vocal register matters:** melodies/partials in the human vocal range (~200–800 Hz) chill more. Consider a subtle vocal-formant layer.
4. **Make chill moments optional but cued:** since sweet spots vary individually, expose "intensity of swells" as a control, not a fixed behavior.

---

## 2. Awe and vastness in music

### Core findings
- **Keltner & Haidt's awe framework:** awe = perception of **vastness** + need for **accommodation** (mental models fail). Bodily markers overlap with frisson: chills, goosebumps. Music is one of the most reliable awe inducers (Silvia et al. 2015; Konecni 2005 — cited in https://dornsife.usc.edu/brainandmusic/wp-content/uploads/sites/265/2023/10/Reflections_on_music_affect_and_sociality.pdf).
- **Fear-adjacent acoustics:** Huron notes passages that induce "sublime" experiences share acoustic features with fear stimuli: **loud sounds, low pitch, infrasound, volume, acoustic proximity/approach, surprise**. The profundity comes from vastness brushing against threat circuitry.
  https://paisano-online.com/12385/arts-life/dr-huron-answers-question-music-gives-us-goosebumps/
- **Tonal dissonance → awe:** a 2026 Frontiers in Psychology paper operationalized awe with the AWE-S scale (vastness, need for accommodation, self-diminishment, time perception, physical sensations, connectedness) and found tonal dissonance evokes complex awe-like states.
  https://public-pages-files-2025.frontiersin.org/journals/psychology/articles/10.3389/fpsyg.2026.1767623/pdf
- **Sacred-space acoustics (Stanford "sound and transcendence" project):** hypothesis that religious awe is driven by acoustics that **disrupt ordinary sensory expectations** — echoes blur spatial boundaries, sound sources become unlocatable, distances stretch/collapse. "Impressions of immensity and indeterminacy" — not knowing where a sound originates or how many sources there are — opens the door to the ethereal.
  https://zenit.org/nota-crudaUS/?id=223698
- **Neural circuits:** Levitin (McGill) — awe-inducing music engages amygdala, ventral tegmental area, nucleus accumbens; modulates dopamine *and* endogenous opioids. Slower music calms heart rate/deepens breathing → felt connectedness.
  https://www.wcmu.org/health-science-and-environment/2023-07-29/these-scientists-explain-the-power-of-music-to-spark-awe

### Honest caveats
- Awe research in music is young; most evidence is self-report + correlational. The Stanford project is investigational (no causal claims yet). "Vastness" is a psychological appraisal, not a dial you can set.

### Actionable takeaways for SG-9
1. **Design for *perceived vastness*:** very long reverb tails (cathedral-scale RT60), diffuse fields, **unlocatable sources** (slow stereo drift, decorrelated L/R reverb, sounds that can't be pinned down). The Stanford hypothesis says indeterminacy of source/space is the awe engine — SG-9's binaural resonance pan already moves in this direction; extend it to reverb.
2. **Slowness + volume swells + low pitch** = the acoustic awe triad. The existing SESSION macro should swell toward a vast peak, not just evolve timbrally.
3. **Controlled dissonance-then-resolution:** brief beating/dissonant intervals that resolve into the chakra fundamental can evoke the "small earthquake in the mind" quality of awe — but keep it brief and resolvable, since unresolved dissonance reads as unpleasant.

---

## 3. Ego dissolution / mystical-type experiences with music

### Core findings
- **Kaelen et al. (2018, "The hidden therapist"):** in psilocybin therapy for treatment-resistant depression, patients' **experience of the music** was associated with **mystical experience and insightfulness**, and — crucially — the nature of the music experience **significantly predicted reductions in depression 1 week later, whereas general drug intensity did not**. Welcome influences: personally meaningful emotion/imagery, sense of guidance, openness, calm/safety. Disliked music was associated with resistance and diminished drug effects.
  https://groundedtravels.nl/wp-content/uploads/2025/01/Artikel-over-Muziek-tijdens-psychedelische-reis-.pdf
- **Music amplifies acute psychedelic states:** a 2025 scoping review concludes the quality of the music experience moderates acute variables like **mystical experience and ego dissolution**, which are known mediators of clinical response — but notes **no study has included a no-music control**, so direct causal inference is limited.
  https://onlinelibrary.wiley.com/doi/10.1002/brb3.71533
- **What music supports *peak* experiences (Barrett et al. 2017, Frontiers in Psychology):** expert-recommended peak-period music was characterized by **regular, predictable, formulaic phrase structure and orchestration; a feeling of continuous movement and forward motion that slowly builds over time; and lower perceptual brightness** than pre-peak music.
  http://pmc.ncbi.nlm.nih.gov/articles/PMC5524670/
  *Note the tension with §1: chills want surprise; peak/mystical support wants predictability + slow build. Different moments, different recipes.*
- **Mechanisms (LSD + music):** Kaelen et al. (2016): under LSD, music modulated parahippocampal connectivity to visual cortex and inferior frontal gyrus — music drives mental imagery via memory systems. Lebedev et al. (2016): brain-entropy increases under LSD were greatest during music-listening scans in participants reporting ego dissolution.
  http://www.biorxiv.org//content/10.1101/153031v1.full
- **Chills-augmented contemplative practice (Christov-Moore et al. 2023):** randomized controlled online study (n=398): loving-kindness meditation + **chills-inducing music** enhanced **self-transcendence, mood, emotional breakthrough, and psychological insight** vs. controls. Mediation analysis: **the occurrence of aesthetic chills predicted the downstream effects**. Trait **absorption** predicted ego dissolution; interoceptive awareness predicted ego dissolution + connectedness.
  https://www.researchgate.net/publication/371564678_Psychedelic_unselfing_self-transcendence_and_change_of_values_in_psychedelic_experiences
- **Monochord evidence:** Sandler et al. (2017, PLOS ONE): body-monochord vibroacoustic stimulation in psychosomatic patients reduced sympathetic arousal (skin conductance) comparably to relaxation music; subjects reported deep relaxation with "inner images and uncommon sensations" — i.e., drone + felt vibration nudges toward inward, image-rich states.
  https://pubmed.ncbi.nlm.nih.gov/28114399/
- **Musical trance neuroscience (Open Book Publishers chapter):** trance musics favor **drone continuums and pulsation** — simple, repeating sonic materials processed in early auditory chains without invoking long-term musical memory; associated with **decreased posterior cingulate (DMN) activity**. "Nonprogressive music has more potential in directing attention to the very moment of the musical flow."
  https://books.openbookpublishers.com/10.11647/obp.0403.08.pdf
- **Strong Experiences with Music (Gabrielsson):** content analysis of ~400 "strongest musical experience" narratives: physical reactions (tears, shivers, gooseflesh), altered perception of **body, time, and place**, and existential/transcendental aspects ("losing one's body, dissolution of self, timelessness, eternity, transferred to another world"). Music factors named: genre, **loudness**, instrumentation; personal factors: expectations, need, prior experience.
  http://IJEA.org/v14r7/v14r7.pdf

### Honest caveats
- Psychedelic-music findings are correlational and drug-context-specific; sober transfer is plausible but unproven. Barrett 2017 is expert recommendation (n=10), not an experiment. Gabrielsson's data are retrospective narratives.

### Actionable takeaways for SG-9
1. **Two-zone session design:** *Ascent* = chills recipe (§1: swells, harmonic surprise, appoggiaturas). *Peak* = Barrett recipe (**regular, predictable structure; continuous slow-building forward motion; lower brightness/darker timbre**). The existing Journey/Ascension mode is the right skeleton — formalize the ascent→peak→return arc with distinct DSP behaviors per phase.
2. **Music must never be disliked or resisted:** Kaelen's negative finding matters — music that jars creates resistance and *blocks* depth. Provide a "gentle/safe" guarantee: no harsh transients, no unpredictable loud events in peak phase; user-adjustable intensity.
3. **Target DMN-quieting:** drones + monotony reduce self-referential processing (PCC/DMN). Keep melodic/structural complexity low during peak — the profundity comes from *less* information, not more.
4. **Chills as a causal lever:** Christov-Moore suggests engineering actual chill moments mid-session causally boosts downstream self-transcendence and insight — not just correlation. Treat composed swell/arrival moments as the highest-value DSP events in the session.
5. **Absorption is the user trait that predicts ego dissolution** — UX can't change traits, but it can *select for and scaffold* absorptive listening (see §6).

---

## 4. Low-frequency physical sensation (vibroacoustic)

### Core findings
- **40 Hz is the workhorse frequency** in vibroacoustic therapy research — most commonly used as standalone or fundamental; induces whole-body relaxation response. Parameters: frequency, amplitude (loudness), pulsation rate, direction.
  https://www.mdpi.com/2227-9032/10/6/1024
- **Body resonance bands:** abdominal resonance ~4–8 Hz; head-neck 20–30 Hz; eyeball 20–90 Hz (mechanical vibration literature). Above ~100 Hz simple body models break down.
  http://www.gbppr.net/mil/mindcontrol/l59_04.asp.html
- **Chest vs. abdomen:** Takahashi et al. (1999): pure tones 20–50 Hz produced measurable body-surface vibration; **chest vibrated more than abdomen**, increasing with frequency; effect negatively correlated with BMI.
  https://www.jstage.jst.go.jp/article/indhealth1963/37/1/37_1_28/_article/-char/en
- **Infrasound and unease:** Tandy & Lawrence (1998) linked ~19 Hz infrasound to feelings of awe/fear and "presence" (haunting phenomena). Low frequencies can create feelings of awe *and* fear and enhance perception.
  https://www.researchgate.net/publication/357661497_MSc_Dissertation_Physiological_and_emotional_effects_of_sound_interacting_with_megalith_acoustics
- **EEG + vibroacoustic + mindfulness:** whole-body low-frequency vibration deepens meditative states beyond audio alone; participants report "felt in my cells... spreading in my chest... I'm in awe — I have a really deep calmness. Compared to meditation it goes way deeper in the body."
  https://www.mdpi.com/2813-9844/7/4/80

### Honest caveats
- Precise "chest resonance" figures vary across sources (40–80 Hz production lore vs. mechanical-resonance literature at lower bands); the honest claim is a **felt-bass zone (~40–80 Hz) that couples to the torso**, not a single magic number. Tandy & Lawrence's 19 Hz work is a famous single study, partially replicated at best.

### Actionable takeaways for SG-9
1. **Keep a dedicated felt-bass band (40–80 Hz)** with enough level to be physically felt on real speakers — this is the embodiment channel that turns "hearing" into "being inside" the sound. Monitors/headphones that can't reproduce it lose this effect; consider a "sub-bass" level control and note speaker requirements in UX.
2. **Use 40 Hz as an anchor** (therapeutic literature's most-used frequency) — e.g., a sub-oscillator or the difference-tone layer.
3. **Avoid sustained energy in the ~17–20 Hz infrasound band** at high level unless deliberately courting unease; awe without the fear edge lives higher.
4. **Pulsation (amplitude modulation) of the bass** in the theta range (see §7) couples body felt-sense with trance rhythm — a single modulator driving both.

---

## 5. Duration thresholds

### Core findings
- **20 minutes is the empirical hinge:** a Scientific Reports study (n=372) directly compared 10 vs. 20 min single-session mindfulness: no overall dose difference in state mindfulness, but 20 min beat 10 min on state-anxiety reduction in high-trait-mindfulness participants.
  https://www.nature.com/articles/s41598-023-46578-y
- **20 min open-monitoring meditation** produced measurable EEG changes (error-monitoring signals) vs. controls (Michigan State, Brain Sciences 2019).
  https://www.news-medical.net/news/20191112/Just-20-minutes-of-meditation-makes-a-difference.aspx
- **Shamanic drumming:** 15 min of monotonous drumming at 8 Hz with journey instructions efficiently induced shamanic imagery in studies (Rock et al., cited in https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0102103). Traditional practice uses **hours** (Harner: 3–4.5 pulses/sec for several hours).
- **Psychedelic arcs:** psilocybin phenomenology reliably phases as **Ascent → Peak → Descent** (Stenbæk et al. 2021), mirrored in playlist design: onset = calming/reassuring; peak = emotionally evocative/intense; descent = reflective/integrative.
  https://unlimitedscience.glossdev.com/wp-content/uploads/2023/11/fpsyg-13-873455.pdf ; http://www.mindbloom.com/blog/the-role-of-music-in-psychedelic-therapy

### Honest caveats
- No study establishes a hard "profundity threshold" in minutes; 20 min is a converging convention, not a law. Trait mindfulness moderates everything.

### Actionable takeaways for SG-9
1. **Default the SESSION macro to 20 minutes, not 10** — 10 min is an intro/demo length; the literature's deep-state window starts at ~15–20. Keep 10 as "taster" and add 30/45 for committed sessions.
2. **Structure every session as Ascent → Peak → Descent** with distinct musical behavior per phase (mirrors both the psychedelic-therapy playlist convention and the three-phase phenomenology). The existing SESSION macro should be re-mapped onto this arc explicitly.
3. **Don't rush the ascent:** at least the first third should be calming, predictable, grounding before introducing chill events or intensity.

---

## 6. Listener attention, intention, set & setting

### Core findings
- **Intention predicts outcomes:** in psychedelic research, forming a meaningful pre-session intention was (with decentering) the only variable retaining significance for psychological flexibility after controls — more than mystical experience or ego dissolution themselves.
  https://www.akjournals.com/view/journals/2054/9/1/article-p31.xml
- **Active vs. passive listening:** "active music listening" (intentional, attentive) amplifies music's mental-health benefits vs. background listening (music-therapy literature consensus).
- **Set and setting framework (Carhart-Harris):** context — including music — has "enhanced influence in the drug condition vs. placebo," and patients' relationship to the music predicts experience quality → long-term outcomes.
  https://cfnmu.com/wp-content/uploads/2026/08/carhart-harris-et-al-2018-psychedelics-and-the-essential-importance-of-context-1.pdf
- **Chills-augmented meditation (Christov-Moore 2023):** the *combination* of guided contemplative framing + chill-inducing music beat either alone; chills occurrence mediated the transcendence effects. Framing + music are synergistic, not additive.
- **Trait absorption** (tendency to become immersed) independently predicted ego dissolution and connectedness — the single most relevant listener trait.

### Honest caveats
- Effect sizes for "intention" are correlational in survey data; the Christov-Moore RCT is the strongest causal evidence and it's online/self-report.

### Actionable takeaways for SG-9 (UX — this may be the highest-ROI section)
1. **Pre-session intention ritual:** a 30-second onboarding — "set an intention for this session" (typed or chosen), headphones on, eyes closed, dim lights. This is not decoration; it's the best-evidenced profundity lever in the literature.
2. **Instruct active listening:** explicitly tell users not to background it — "listen *into* the sound; follow a single tone." Absorption is trainable in-session via guidance.
3. **Set & setting checklist:** dark/quiet room, comfortable position, no interruptions, good speakers or headphones. Frame the session as a *journey* (Ascent/Peak/Return naming does this).
4. **Post-session integration:** a brief "what did you notice?" prompt + journal. Psychedelic therapy's integration phase is part of why experiences become "transformative" rather than merely intense.

---

## 7. Rhythmic entrainment and trance (drumming literature)

### Core findings
- **Neher (1961, 1962):** auditory driving observed with scalp EEG; proposed a physiological explanation of drum-ceremony behaviors — rhythmic auditory stimulation can drive brain electrical activity.
- **The theta hypothesis:** shamanic drumming worldwide clusters at **3–7 beats/sec** (Harner: 3–4.5 pulses/sec for hours; Jilek's Salish research: 4–7/sec; Goodman: 200–210 bpm ≈ 3.3–3.5 Hz), matching the **theta EEG band (4–8 Hz)** associated with hypnagogic imagery, meditation, and ecstasy.
  http://www.ece.uah.edu/~jovanov/papers/B2011_Jovanov_Entraining_the_brain-body.pdf
  https://www.cuyamungueinstitute.com/articles-and-news/rhythmic-sound-trance-states-and-the-brain/
- **Will & Berg (2007):** periodic acoustic stimuli at 1–8 Hz significantly increased brain-wave synchronization/entrainment.
- **Pattern matters:** a Swedish EEG study found 4.5 beats/sec drumming elicited a strong increase in the **theta** component (plus alpha/beta) — different patterns drive different bands.
- **PLOS ONE shamanic journeying study (2014):** repetitive drumming (4–7 Hz) + shamanic instructions induced specific subjective experiences (imagery); **but cortisol decrease was no larger than with instrumental meditation music** — important negative finding: drumming isn't a uniquely powerful relaxant; its value is the *imagery/trance* content.
  https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0102103
- **Monotony is the mechanism:** Berger & Turow review five theories; the entrainment-by-monotony account holds that *unvarying* repetition (not rhythmic complexity) drives the effect. Harner: the drumming must be steady and monotonous for extended periods.

### Honest caveats
- "Theta entrainment" is an attractive story; direct EEG evidence during actual trance is thin and mixed. Much of the literature is anthropological + small lab studies. The cortisol null result is a useful reality check.

### Actionable takeaways for SG-9
1. **Add a theta-rate pulse option to the drums/pulse layer:** anchor rates at **~4.5 Hz (270 bpm)** and **8 Hz**, presented as "trance rates." Keep it *monotonous* during trance segments — variation breaks the driving effect. (SG-9 already split pulse/drums; make the pulse layer capable of strict, unvarying theta monotony.)
2. **Couple the pulse to the felt-bass band** (§4): a 4–7 Hz amplitude modulation on the 40–80 Hz bed = body-felt trance driver.
3. **Don't oversell complexity:** the trance literature says *less* rhythmic variation, not more. Save the tabla/hand-percussion vocabulary for ascent/descent; hold the peak in monotony.
4. **Duration applies:** trance induction needs 10–15+ min of uninterrupted driving — another vote for 20-min default sessions.

---

## Cross-cutting design synthesis

| Principle | Source | SG-9 implementation |
|---|---|---|
| Predictable bed + intermediate-novelty events (Wundt) | §1 | Drone foundation static; composed swells/harmonic arrivals on top |
| Chills = anticipation → release (dopamine) | §1 | Slow crescendos resolving into harmonic arrivals; appoggiatura resolutions onto fundamentals |
| Vastness = unlocatable sources + huge space + slowness + low end | §2 | Cathedral RT60, decorrelated drifting reverb, sub-bass, slow swells |
| Peak = predictable, slow-building, dark, forward-moving | §3 | Barrett recipe for the peak phase; dissonance resolves quickly |
| Felt bass 40–80 Hz; avoid dwelling at ~19 Hz | §4 | Dedicated sub band, 40 Hz anchor, level control |
| 20-min default session; Ascent→Peak→Descent arc | §5 | Re-map SESSION macro; 10 min = taster only |
| Intention + active listening + setting = biggest lever | §6 | Pre-session ritual, intention prompt, listening instructions, integration prompt |
| Theta-rate (4.5–8 Hz) monotony for trance | §7 | Pulse layer trance-rate presets; minimal variation in peak |

**Single most important honest summary:** no feature of sound *guarantees* profundity. The literature supports a probabilistic recipe — predictable vast drone (DMN quieting) + composed chill moments (dopamine/anticipation) + felt bass (embodiment) + ≥20 min arc (ascent/peak/descent) + intentional, absorptive listening (set/setting) — each with small-to-moderate, mostly correlational evidence, strongest where randomized (Christov-Moore chills-augmentation) or pharmacological (Ferreri dopamine) designs exist. The UX/set-setting layer likely contributes as much variance as the DSP.
