#include <../headers/timestretch.h>

vector<vector<double>> TimeStretch::changeSpeed(vector<vector<double>> samples, double shiftFactor)
{
    int numChannels = samples.size();
    vector<vector<double>> output(numChannels, vector<double>(samples[0].size() / shiftFactor, 0.0));
    vector<thread> threads;

    // Process each channel independently
    for (int chan = 0; chan < numChannels; chan++)
    {
        threads.emplace_back(stretchChannel, samples[chan], shiftFactor, ref(output[chan]));
    }

    for (thread &t : threads)
    {
        t.join();
    }
    Utils::normalize(output);
    return output;
}

void TimeStretch::stretchChannel(const vector<double>& channel, double shiftFactor, vector<double>& out)
{
    vector<thread> threads;
    int hopSize = 750000;

    for (int i = 0; i < channel.size(); i += hopSize)
    {
        int end = min(i + hopSize , (int)channel.size() - 1);
        vector<double> slice(channel.begin() + i, channel.begin() + end);
        threads.emplace_back(stretchFrame, slice, (double) i / shiftFactor, shiftFactor, ref(out));
    }

    for (thread& t : threads)
    {
        t.join();
    }
    
}
void TimeStretch::stretchFrame(const vector<double>& channel, int shiftedStart, double shiftFactor, vector<double> &out)
{
    int step = PITCH_CHUNK_SZ / 4;
    int frameSize = 4096;
    int hopSize = 1500;
    int n = round(((channel.size()) / hopSize) + 1);
    vector<vector<complex<double>>> stftBins(n, vector<complex<double>>(frameSize));

    FFT handler = FFT(Utils::nextPowerOfTwo(frameSize));

    for (int start = 0, j = 0; start < channel.size() && j < n; start += hopSize, j++)
    {
        int end = min(start + frameSize, (int)channel.size() - 1);
        vector<double> slice(channel.begin() + start, channel.begin() + end);
        Utils::applyWindow(slice);
        stftBins[j] = handler.fft(slice);
    }

    int newBinCnt = round(n / shiftFactor);
    vector<vector<complex<double>>> newStftBins(newBinCnt, vector<complex<double>>(frameSize));
    vector<double> lastPhases(frameSize);
    vector<double> phaseSum(frameSize, 0.0);
    for (int i = 0; i < newBinCnt; i++)
    {
        double og = i * shiftFactor;
        int low = (floor(og) < n) ? floor(og) : n - 1;
        int high = (ceil(og) < n) ? ceil(og) : n - 1;
        double alpha = og - low;
        vector<complex<double>> lowBin = stftBins[low];
        vector<complex<double>> highBin = stftBins[high];

        newStftBins[i] = Utils::addComplex(Utils::scaleComplex(lowBin, 1 - alpha), Utils::scaleComplex(highBin, alpha));

        /* Ensure phase coherence */
        for (int j = 0; j < frameSize; j++)
        {
            /* TODO use a data structure, to save the phase information from the previous frame's edge to align with the beginning of new frame */
            double phaseDiff = arg(newStftBins[i][j]);
            double lastPhase = 0.0;
            double lastMagnitude = 0.0;
            if (i > 0)
            {
                lastPhase = arg(newStftBins[i-1][j]);
                lastMagnitude = abs(newStftBins[i-1][j]);
                phaseDiff -= lastPhase;

            }

            /* Transient detection, if transient, do nothing to keep timbre */
            if (abs(newStftBins[i][j]) <= 2.0 * lastMagnitude) 
            {
                /* Perform phase unwrapping */
                while (phaseDiff > M_PI)
                    phaseDiff -= 2 * M_PI;
                while (phaseDiff < -M_PI)
                    phaseDiff += 2 * M_PI;

                newStftBins[i][j] = polar(abs(newStftBins[i][j]), lastPhase + phaseDiff);
            }
        }
    }
    for (int start = 0, i = 0; start < out.size() && i < newBinCnt; start += hopSize, i++)
    {
        vector<double> inverse = handler.ifft(newStftBins[i]);
        Utils::applyWindow(inverse);
        for (int j = 0; j < inverse.size() && j +  shiftedStart + start < out.size(); j++)
        {
            out[j + shiftedStart + start] += inverse[j];
        }
    }
    
}

// void TimeStretch::processSlice(vector<double> grain, int shiftedStart, vector<double> &out)
// {
//     for (int i = 0; i < grain.size(); i++)
//     {
//         if (i + shiftedStart < out.size())
//         {
//             out[i + shiftedStart] += grain[i];
//         }
//     }
// }