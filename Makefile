BUILDDIR  := build
WAV_CMP   := $(BUILDDIR)/tests/wav_compare
INPUT_DIR := tests/golden/input
OUT_DIR   := tests/output

.PHONY: golden validate compare clean-output

# ------------------------------------------------------------------
# golden: generate reference input WAVs + Python golden output WAVs.
# Run once (or after Python effect algorithms change).
# ------------------------------------------------------------------
golden:
	uv run --project python python/generate_golden.py

# ------------------------------------------------------------------
# validate: build wav_compare, run C++ effects on every golden input,
# write results to tests/output/.
# ------------------------------------------------------------------
$(WAV_CMP):
	cmake -B $(BUILDDIR) -G Ninja
	cmake --build $(BUILDDIR) --target wav_compare

validate: $(WAV_CMP)
	mkdir -p $(OUT_DIR)
	$(WAV_CMP) overdrive $(INPUT_DIR)/medium.wav $(OUT_DIR)/overdrive_default.wav        --drive 0.5 --tone 0.5 --level 0.8
	$(WAV_CMP) overdrive $(INPUT_DIR)/medium.wav $(OUT_DIR)/overdrive_drive_high.wav     --drive 1.0 --tone 0.5 --level 0.8
	$(WAV_CMP) overdrive $(INPUT_DIR)/medium.wav $(OUT_DIR)/overdrive_tone_bright.wav    --drive 0.5 --tone 0.9 --level 0.8
	$(WAV_CMP) overdrive $(INPUT_DIR)/hard.wav   $(OUT_DIR)/overdrive_hard_input.wav     --drive 0.5 --tone 0.5 --level 0.8
	$(WAV_CMP) overdrive $(INPUT_DIR)/medium.wav $(OUT_DIR)/overdrive_softclip.wav       --drive 0.5 --tone 0.5 --level 0.8 --mode softclip
	$(WAV_CMP) overdrive $(INPUT_DIR)/medium.wav $(OUT_DIR)/overdrive_foldback.wav       --drive 0.5 --tone 0.5 --level 0.8 --mode foldback
	$(WAV_CMP) overdrive $(INPUT_DIR)/medium.wav $(OUT_DIR)/overdrive_asymmetric.wav     --drive 0.5 --tone 0.5 --level 0.8 --mode asymmetric
	$(WAV_CMP) overdrive $(INPUT_DIR)/medium.wav $(OUT_DIR)/overdrive_bitcrush.wav       --drive 0.5 --tone 0.5 --level 0.8 --mode bitcrush
	$(WAV_CMP) overdrive $(INPUT_DIR)/medium.wav $(OUT_DIR)/overdrive_midfocus.wav       --drive 0.5 --tone 0.5 --level 0.8 --shape midfocus
	$(WAV_CMP) overdrive $(INPUT_DIR)/medium.wav $(OUT_DIR)/overdrive_brightfocus.wav    --drive 0.5 --tone 0.5 --level 0.8 --shape brightfocus
	$(WAV_CMP) overdrive $(INPUT_DIR)/medium.wav $(OUT_DIR)/overdrive_mid_boost.wav      --drive 0.5 --tone 0.5 --level 0.8 --mid 6.0
	$(WAV_CMP) overdrive $(INPUT_DIR)/medium.wav $(OUT_DIR)/overdrive_presence.wav       --drive 0.5 --tone 0.5 --level 0.8 --presence 4.0
	$(WAV_CMP) overdrive $(INPUT_DIR)/medium.wav $(OUT_DIR)/overdrive_bias_pos.wav       --drive 0.5 --tone 0.5 --level 0.8 --mode softclip --bias  0.3
	$(WAV_CMP) overdrive $(INPUT_DIR)/medium.wav $(OUT_DIR)/overdrive_bias_neg.wav       --drive 0.5 --tone 0.5 --level 0.8 --mode softclip --bias -0.3
	$(WAV_CMP) cabinet   $(INPUT_DIR)/medium.wav $(OUT_DIR)/cabinet_medium.wav
	$(WAV_CMP) cabinet   $(INPUT_DIR)/hard.wav   $(OUT_DIR)/cabinet_hard.wav
	$(WAV_CMP) cabinet   $(INPUT_DIR)/medium.wav $(OUT_DIR)/cabinet_4x12_medium.wav  --type 4x12
	$(WAV_CMP) cabinet   $(INPUT_DIR)/hard.wav   $(OUT_DIR)/cabinet_4x12_hard.wav    --type 4x12
	$(WAV_CMP) cabinet   $(INPUT_DIR)/medium.wav $(OUT_DIR)/cabinet_combo_medium.wav --type combo
	$(WAV_CMP) cabinet   $(INPUT_DIR)/hard.wav   $(OUT_DIR)/cabinet_combo_hard.wav   --type combo
	$(WAV_CMP) delay     $(INPUT_DIR)/medium.wav $(OUT_DIR)/delay_default.wav          --time-ms 300 --feedback 0.4 --mix 0.5
	$(WAV_CMP) delay     $(INPUT_DIR)/medium.wav $(OUT_DIR)/delay_long.wav             --time-ms 500 --feedback 0.6 --mix 0.5
	$(WAV_CMP) delay     $(INPUT_DIR)/bass.wav   $(OUT_DIR)/delay_bass.wav             --time-ms 300 --feedback 0.4 --mix 0.5
	$(WAV_CMP) delay     $(INPUT_DIR)/medium.wav $(OUT_DIR)/delay_wow.wav              --time-ms 300 --feedback 0.4 --mix 0.5 --wow-rate 0.5 --wow-depth 4.0
	$(WAV_CMP) delay     $(INPUT_DIR)/medium.wav $(OUT_DIR)/delay_flutter.wav          --time-ms 300 --feedback 0.4 --mix 0.5 --flutter-rate 8.0 --flutter-depth 1.0
	$(WAV_CMP) delay     $(INPUT_DIR)/medium.wav $(OUT_DIR)/delay_tape_sat.wav         --time-ms 300 --feedback 0.4 --mix 0.5 --tape-sat
	$(WAV_CMP) delay     $(INPUT_DIR)/medium.wav $(OUT_DIR)/delay_tape_age.wav         --time-ms 300 --feedback 0.4 --mix 0.5 --tape-sat --tape-age 0.8
	$(WAV_CMP) delay     $(INPUT_DIR)/medium.wav $(OUT_DIR)/delay_duck.wav            --time-ms 300 --feedback 0.4 --mix 0.5 --duck-threshold -20 --duck-depth 0.5
	$(WAV_CMP) delay     $(INPUT_DIR)/medium.wav $(OUT_DIR)/delay_duck_deep.wav       --time-ms 300 --feedback 0.4 --mix 0.5 --duck-threshold -20 --duck-depth 1.0
	$(WAV_CMP) delay     $(INPUT_DIR)/medium.wav $(OUT_DIR)/delay_diffusion_half.wav  --time-ms 300 --feedback 0.4 --mix 0.5 --diffusion 0.5
	$(WAV_CMP) delay     $(INPUT_DIR)/medium.wav $(OUT_DIR)/delay_diffusion_full.wav  --time-ms 300 --feedback 0.4 --mix 0.5 --diffusion 1.0
	$(WAV_CMP) delay     $(INPUT_DIR)/medium.wav $(OUT_DIR)/delay_stereo_independent.wav --time-ms 300 --feedback 0.4 --mix 0.5 --stereo
	$(WAV_CMP) delay     $(INPUT_DIR)/medium.wav $(OUT_DIR)/delay_stereo_pingpong.wav    --time-ms 300 --feedback 0.4 --mix 0.5 --ping-pong

# ------------------------------------------------------------------
# compare: validate + diff every golden WAV against its C++ output.
# Exits non-zero if any pair exceeds the 1e-5 tolerance.
# ------------------------------------------------------------------
compare: validate
	uv run --project python python/compare.py --all

clean-output:
	rm -rf $(OUT_DIR)
