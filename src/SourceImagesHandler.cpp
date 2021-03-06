#include "SourceImagesHandler.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

void SourceImagesHandler::prepareToPlay(const unsigned int samplesPerBlockExpected, const double sampleRate)
// Local equivalent of prepareToPlay
{
	// prepare buffers
	workingBuffer.setSize(1, samplesPerBlockExpected);
	workingBuffer.clear();
	workingBufferTemp = workingBuffer;
	clipboardBuffer = workingBuffer;
	bandBuffer.setSize(NUM_OCTAVE_BANDS, samplesPerBlockExpected);
	binauralBuffer.setSize(2, samplesPerBlockExpected);

	// keep local copies
	localSampleRate = sampleRate;
	localSamplesPerBlockExpected = samplesPerBlockExpected;

	// init filter bank
	filterBank.prepareToPlay(samplesPerBlockExpected, sampleRate);
	filterBank.setNumFilters(NUM_OCTAVE_BANDS, current->ids.size());

	// init reverb tail
	reverbTail.prepareToPlay(samplesPerBlockExpected, sampleRate);
	tailBuffer.setSize(reverbTail.fdnOrder, samplesPerBlockExpected);

	// init binaural encoder
	binauralEncoder.prepareToPlay(samplesPerBlockExpected, sampleRate);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

float SourceImagesHandler::getMaxDelayFuture()
// Get max source image delay in seconds
{
	float maxDelayTap = getMaxValue(future->delays);
	return maxDelayTap;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void SourceImagesHandler::getNextAudioBlock(DelayLine<float>* delayLine, AudioBuffer<float>& ambisonicBuffer)
// Main: loop over sources images, apply delay + room coloration + spatialization
{

	// update crossfade mechanism
	updateCrossfade();

	// clear output buffer (since used as cumulative buffer, iteratively summing sources images buffers)
	ambisonicBuffer.clear();

	// loop over sources images
	for (int j = 0; j < numSourceImages; j++)
	{

		//==========================================================================
		// GET DELAYED BUFFER
		float delayInFractionalSamples = 0.0;
		if (!crossfadeOver) // Add old and new tapped delayed buffers with gain crossfade
		{
			// get old delay, tap from delay line, apply gain=f(delay)
			if (j < current->delays.size())
			{
				delayInFractionalSamples = current->delays[j] * localSampleRate;
				delayLine->fillBufferWithPreciselyDelayedChunk(workingBuffer, 0, 0, 0, delayInFractionalSamples, localSamplesPerBlockExpected);
				workingBuffer.applyGain(1.0 - crossfadeGain);
			}
			else { workingBuffer.clear(); }

			// get new delay, tap from delay line, apply gain=f(delay)
			if (j < future->delays.size())
			{
				delayInFractionalSamples = future->delays[j] * localSampleRate;
				delayLine->fillBufferWithPreciselyDelayedChunk(workingBufferTemp, 0, 0, 0, delayInFractionalSamples, localSamplesPerBlockExpected);
				workingBufferTemp.applyGain(crossfadeGain);
			}
			else { workingBufferTemp.clear(); }

			// add both buffers
			workingBuffer.addFrom(0, 0, workingBufferTemp, 0, 0, localSamplesPerBlockExpected);
		}
		else // simple update
		{
			// get delay, tap from delay line
			if (j < current->delays.size())
			{
				delayInFractionalSamples = (current->delays[j] * localSampleRate);
				delayLine->fillBufferWithPreciselyDelayedChunk(workingBuffer, 0, 0, 0, delayInFractionalSamples, localSamplesPerBlockExpected);
			}
		}

		//==========================================================================
		// APPLY GAIN BASED ON SOURCE IMAGE PATH LENGTH
		float gainDelayLine = 0.0f;
		if (!crossfadeOver)
		{
			if (j < current->pathLengths.size()) { gainDelayLine += (1.0 - crossfadeGain) * (1.0 / current->pathLengths[j]); }
			if (j < future->pathLengths.size()) { gainDelayLine += crossfadeGain * (1.0 / future->pathLengths[j]); }
		}
		else
		{
			if (j < current->pathLengths.size())
			{
				gainDelayLine = 1.0 / current->pathLengths[j];
			}
		}
		workingBuffer.applyGain(fmin(1.0, fmax(0.0, gainDelayLine)));

		//==========================================================================
		// APPLY FREQUENCY SPECIFIC GAINS (ABSORPTION, DIRECTIVITY)

		// decompose in frequency bands
		//DBG( "Number of images sources in getNextAudioBlock = " << numSourceImages);
		filterBank.decomposeBuffer(workingBuffer, bandBuffer, j);

		// apply absorption gains and recompose
		workingBuffer.clear();
		float absorptionCoef, dirGain;
		for (int k = 0; k < bandBuffer.getNumChannels(); k++)
		{
			absorptionCoef = 0.f;
			dirGain = 0.f;

			// apply crossfade
			if (!crossfadeOver)
			{
				if (j < current->absorptionCoefs.size())
				{
					absorptionCoef += (1.0 - crossfadeGain) * current->absorptionCoefs[j][k];
					dirGain += (1.0 - crossfadeGain) * current->directivityGains[j][k];
				}
				if (j < future->absorptionCoefs.size())
				{
					absorptionCoef += crossfadeGain * future->absorptionCoefs[j][k];
					dirGain += crossfadeGain * future->directivityGains[j][k];
				}
			}
			else
			{
				if (j < current->absorptionCoefs.size())
				{
					absorptionCoef = current->absorptionCoefs[j][k];
					dirGain = current->directivityGains[j][k]; // only using real part here
				}
			}

			// bound gains
			absorptionCoef = fmin(1.0, fmax(0.0, 1.f - absorptionCoef));
			dirGain = fmin(1.0, fmax(0.0, dirGain));

			// apply absorption gains (TODO: sometimes crashes here at startup because absorptionCoefs data is null pointer)
			bandBuffer.applyGain(k, 0, localSamplesPerBlockExpected, absorptionCoef);

			// apply directivity gain (TODO: merge with absorption gain above)
			bandBuffer.applyGain(k, 0, localSamplesPerBlockExpected, dirGain);

			// recompose (add-up frequency bands)
			workingBuffer.addFrom(0, 0, bandBuffer, k, 0, localSamplesPerBlockExpected);
		}

		//==========================================================================
		// FEED REVERB TAIL FDN
		if (enableReverbTail)
		{
			int busId = j % reverbTail.fdnOrder;
			reverbTail.addToBus(busId, bandBuffer);
		}

		//==========================================================================
		// APPLY DIRECT PATH / EARLY GAINS
		if (j < current->ids.size() && directPathId == current->ids[j])
		{
			workingBuffer.applyGain(directPathGain);
		}
		else
		{
			workingBuffer.applyGain(earlyGain);
		}

		//==========================================================================
		// BINAURAL ENCODING (DIRECT PATH ONLY)
		if (enableDirectToBinaural && j < current->ids.size() && directPathId == current->ids[j])
		{
			// apply filter
			binauralEncoder.encodeBuffer(workingBuffer, binauralBuffer);

			// manual loudness normalization (todo: handle this during hrir filter creation)
			binauralBuffer.applyGain(3.7f);

			// add to output
			ambisonicBuffer.copyFrom(0, 0, binauralBuffer, 0, 0, localSamplesPerBlockExpected);
			ambisonicBuffer.copyFrom(1, 0, binauralBuffer, 1, 0, localSamplesPerBlockExpected);

			// skip remaining (ambisonic encoding)
			continue;
		}

		//==========================================================================
		// AMBISONIC ENCODING

		// keep local copy since working buffer will be used to store ambisonic buffers
		clipboardBuffer = workingBuffer;

		for (int k = 0; k < N_AMBI_CH; k++)
		{
			// create working copy
			workingBuffer = clipboardBuffer;

			if (!crossfadeOver)
			{
				// create 2nd working copy
				workingBufferTemp = clipboardBuffer;

				// apply ambisonic gain past
				if (j < current->ambisonicGains.size())
				{
					workingBuffer.applyGain((1.0 - crossfadeGain) * current->ambisonicGains[j][k]);
				}

				// apply ambisonic gain future
				if (j < future->ambisonicGains.size())
				{
					workingBufferTemp.applyGain(crossfadeGain * future->ambisonicGains[j][k]);
				}

				// add past / future buffers
				workingBuffer.addFrom(0, 0, workingBufferTemp, 0, 0, localSamplesPerBlockExpected);
			}
			else
			{
				// apply ambisonic gain
				if (j < current->ambisonicGains.size())
				{
					workingBuffer.applyGain(current->ambisonicGains[j][k]);
				}
			}

			// iteratively fill in general ambisonic buffer with source image buffers (cumulative)
			ambisonicBuffer.addFrom(2 + k, 0, workingBuffer, 0, 0, localSamplesPerBlockExpected);
		}
	}

	//==========================================================================
	// ADD REVERB TAIL

	if (enableReverbTail)
	{
		// get tail buffer
		reverbTail.extractBusToBuffer(tailBuffer);

		// apply gain
		tailBuffer.applyGain(reverbTailGain);

		// add to ambisonic channels
		int ambiId; int fdnId;
		for (int k = 0; k < fmin(N_AMBI_CH, reverbTail.fdnOrder); k++)
		{
			ambiId = k % 4; // only add reverb tail to WXYZ
			fdnId = k % reverbTail.fdnOrder;
			ambisonicBuffer.addFrom(2 + ambiId, 0, tailBuffer, fdnId, 0, localSamplesPerBlockExpected);
		}
	}

}

///////////////////////////////////////////////////////////////////////////////////////////////////

void SourceImagesHandler::updateFromOscHandler(OSCHandler& oscHandler)
// Update local attributes based on latest received OSC info
{
	// method should not be called while crossfade active.
	// called by MainContentComponent::updateOnOscReveive that checks if crossfadeOver == true.
	// should not need: if( !crossfadeOver ){ return; }

	future->ids = oscHandler.getSourceImageIDs();
	


	//DBG("current->ids :");
	//for(auto id : current->ids)
	//{
	//	DBG(id);
	//}

	//DBG("future->ids :");
	//for(auto id : future->ids)
	//{
	//	DBG(id);
	//}
	
	future->delays = oscHandler.getSourceImageDelays();
	future->pathLengths = oscHandler.getSourceImagePathsLength();
	directPathId = oscHandler.getDirectPathId();

	// update absorption coefficients
	future->absorptionCoefs.resize(future->ids.size());
	for (int j = 0; j < future->ids.size(); j++)
	{
		future->absorptionCoefs[j] = oscHandler.getSourceImageAbsorption(future->ids[j]);
		if (filterBank.numOctaveBands == 3)
		{
			future->absorptionCoefs[j] = from10to3bands(future->absorptionCoefs[j]);
		}
	}

	// update directivity gains
	auto sourceImageDODs = oscHandler.getSourceImageDODs();

	future->directivityGains.resize(future->ids.size());
	for (int j = 0; j < future->ids.size(); j++)
	{
		future->directivityGains[j] = directivityHandler.getGains(sourceImageDODs[j](0), sourceImageDODs[j](1));
		if (filterBank.numOctaveBands == 3)
		{
			future->directivityGains[j] = from10to3bands(future->directivityGains[j]);
		}
	}

	// update reverb tail (even if not enabled, not cpu demanding and that way it's ready to use)
	reverbTail.updateInternals(oscHandler.getRT60Values());

	// save (compute) new Ambisonic gains
	auto sourceImageDOAs = oscHandler.getSourceImageDOAs();

	future->ambisonicGains.resize(future->ids.size());
	for (int i = 0; i < future->ids.size(); i++)
	{
		future->ambisonicGains[i] = ambisonicEncoder.calcParams(sourceImageDOAs[i](0), sourceImageDOAs[i](1));
	}

	// update binaural encoder (even if not enabled, not cpu demanding and that way it's ready to use)
	if (current->ids.size() > 0 && directPathId > -1)
	{
		int directPathIndex = distance(future->ids.begin(), find(future->ids.begin(), future->ids.end(), directPathId));
		binauralEncoder.setPosition(sourceImageDOAs[directPathIndex](0), sourceImageDOAs[directPathIndex](1));
	}

	// update filter bank size
	//DBG("filterBank.setNumFilters(filterBank.numOctaveBands, " << (int) future->ids.size() << ");");
	filterBank.setNumFilters(filterBank.numOctaveBands, future->ids.size());

	// trigger crossfade mechanism: default
	crossfadeOver = false;
	//numSourceImages = max(current->ids.size(), future->ids.size());
	numSourceImages = future->ids.size();
	crossfadeGain = 0.0;

	// crossfade mechanism: zero image source scenario (make sure MainComponent continues to play unprocessed input)
	if (future->ids.size() == 0) {
		crossfadeGain = 1.0;
		updateCrossfade();
		numSourceImages = 0;
	}

}

///////////////////////////////////////////////////////////////////////////////////////////////////

void SourceImagesHandler::setFilterBankSize(const unsigned int numFreqBands)
{
	filterBank.setNumFilters(numFreqBands, current->ids.size());
	bandBuffer.setSize(numFreqBands, localSamplesPerBlockExpected);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void SourceImagesHandler::updateCrossfade()
// Update crossfade mechanism (to avoid zipper noise with smooth gains transitions)
{

	// either update crossfade
	if (crossfadeGain < 1.0)
	{
		crossfadeGain = fmin(crossfadeGain + crossfadeStep, 1.0);
	}
	// or stop crossfade mechanism if not already stopped
	else if (!crossfadeOver)
	{
		// set past = future
		// (objective: atomic swap to make sure no value is updated in middle of audio processing loop)
		std::swap(current, future);

		// reset crossfade internals
		crossfadeGain = 1.0; // just to make sure for the last loop using crossfade gain
		crossfadeOver = true;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////