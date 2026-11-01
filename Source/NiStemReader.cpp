#include "NiStemReader.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <vector>

namespace
{
	struct FormatCloser { void operator() (AVFormatContext* c) const { avformat_close_input (&c); } };
	struct CodecCloser  { void operator() (AVCodecContext* c) const  { avcodec_free_context (&c); } };
	struct PacketFree   { void operator() (AVPacket* p) const        { av_packet_free (&p); } };
	struct FrameFree    { void operator() (AVFrame* f) const         { av_frame_free (&f); } };
	using Format = std::unique_ptr<AVFormatContext, FormatCloser>;
	using Codec = std::unique_ptr<AVCodecContext, CodecCloser>;

	Format openFormat (const juce::File& file)
	{
		// NI's files make FFmpeg warn about harmless things (a timescale, a creation time) on
		// every open: only its errors reach StemDeck's journal.
		static const bool quiet = [] { av_log_set_level (AV_LOG_ERROR); return true; }();
		juce::ignoreUnused (quiet);
		AVFormatContext* context = nullptr;
		if (avformat_open_input (&context, file.getFullPathName().toRawUTF8(), nullptr, nullptr) < 0)
			return {};
		Format format (context);
		if (avformat_find_stream_info (context, nullptr) < 0)
			return {};
		return format;
	}

	// The stream index of the file's `track`-th audio stream; -1 if there is none.
	int audioStreamIndex (const AVFormatContext& format, int track)
	{
		for (unsigned i = 0, audio = 0; i < format.nb_streams; ++i)
			if (format.streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && (int) audio++ == track)
				return (int) i;
		return -1;
	}

	juce::int64 lengthOf (const AVFormatContext& format, const AVStream& stream)
	{
		const auto rate = stream.codecpar->sample_rate;
		if (stream.duration > 0)
			return av_rescale_q (stream.duration, stream.time_base, AVRational { 1, rate });
		return format.duration > 0 ? av_rescale (format.duration, rate, AV_TIME_BASE) : 0;
	}

	// One decoded frame, any of the formats AAC and ALAC decode to, appended as float.
	void append (const AVFrame& frame, std::vector<std::vector<float>>& channels)
	{
		const auto format = (AVSampleFormat) frame.format;
		const bool planar = av_sample_fmt_is_planar (format) != 0;
		const auto numChannels = (int) channels.size();
		// What AAC decodes to: each channel's floats in a plane of their own, copied as a block.
		if (format == AV_SAMPLE_FMT_FLTP)
		{
			for (int channel = 0; channel < numChannels; ++channel)
			{
				const auto* plane = reinterpret_cast<const float*> (frame.extended_data[channel]);
				channels[(size_t) channel].insert (channels[(size_t) channel].end(), plane, plane + frame.nb_samples);
			}
			return;
		}
		const auto sampleAt = [&] (int channel, int i) -> float
		{
			const auto plane = planar ? channel : 0;
			const auto index = planar ? i : i * numChannels + channel;
			const auto* data = frame.extended_data[plane];
			const auto packed = av_get_packed_sample_fmt (format);
			if (packed == AV_SAMPLE_FMT_FLT) return reinterpret_cast<const float*> (data)[index];
			if (packed == AV_SAMPLE_FMT_DBL) return (float) reinterpret_cast<const double*> (data)[index];
			if (packed == AV_SAMPLE_FMT_S16) return reinterpret_cast<const int16_t*> (data)[index] / 32768.0f;
			if (packed == AV_SAMPLE_FMT_S32) return (float) (reinterpret_cast<const int32_t*> (data)[index] / 2147483648.0);
			return 0.0f;   // no AAC or ALAC decoder gives another
		};
		for (int channel = 0; channel < numChannels; ++channel)
			for (int i = 0; i < frame.nb_samples; ++i)
				channels[(size_t) channel].push_back (sampleAt (channel, i));
	}

	// Every frame the decoder has ready; false on a decoding error.
	bool drain (AVCodecContext& codec, AVFrame& frame, std::vector<std::vector<float>>& channels)
	{
		for (;;)
		{
			const auto result = avcodec_receive_frame (&codec, &frame);
			if (result == AVERROR (EAGAIN) || result == AVERROR_EOF)
				return true;
			if (result < 0)
				return false;
			append (frame, channels);
			av_frame_unref (&frame);
		}
	}
}

std::unique_ptr<NiStemReader> NiStemReader::open (const juce::File& file, int track)
{
	const auto format = openFormat (file);
	if (format == nullptr)
		return nullptr;
	const auto index = audioStreamIndex (*format, track);
	if (index < 0)
		return nullptr;

	const auto& stream = *format->streams[index];
	std::unique_ptr<NiStemReader> reader (new NiStemReader (file, index));
	reader->sampleRate = stream.codecpar->sample_rate;
	reader->numChannels = (unsigned int) stream.codecpar->ch_layout.nb_channels;
	reader->lengthInSamples = lengthOf (*format, stream);
	reader->bitsPerSample = 32;
	reader->usesFloatingPointData = true;
	if (reader->sampleRate <= 0.0 || reader->numChannels == 0)
		return nullptr;
	return reader;
}

NiStemReader::NiStemReader (const juce::File& f, int index)
	: juce::AudioFormatReader (nullptr, "NI Stems"), file (f), streamIndex (index)
{
}

// The whole track into `samples`; its own demuxer, as the reading thread is not the opening one.
bool NiStemReader::decodeOnce()
{
	if (std::exchange (decodeTried, true))
		return decoded;

	const auto format = openFormat (file);
	if (format == nullptr || streamIndex >= (int) format->nb_streams)
		return false;
	for (unsigned i = 0; i < format->nb_streams; ++i)
		format->streams[i]->discard = (int) i == streamIndex ? AVDISCARD_DEFAULT : AVDISCARD_ALL;

	const auto* parameters = format->streams[streamIndex]->codecpar;
	const auto* decoder = avcodec_find_decoder (parameters->codec_id);
	Codec codec (decoder != nullptr ? avcodec_alloc_context3 (decoder) : nullptr);
	if (codec == nullptr || avcodec_parameters_to_context (codec.get(), parameters) < 0
		|| avcodec_open2 (codec.get(), decoder, nullptr) < 0)
		return false;

	std::unique_ptr<AVPacket, PacketFree> packet (av_packet_alloc());
	std::unique_ptr<AVFrame, FrameFree> frame (av_frame_alloc());
	std::vector<std::vector<float>> channels ((size_t) numChannels);
	for (auto& channel : channels)
		channel.reserve ((size_t) lengthInSamples + 4096);

	while (av_read_frame (format.get(), packet.get()) >= 0)
	{
		const bool ours = packet->stream_index == streamIndex;
		const bool ok = ! ours || (avcodec_send_packet (codec.get(), packet.get()) >= 0 && drain (*codec, *frame, channels));
		av_packet_unref (packet.get());
		if (! ok)
			return false;
	}
	avcodec_send_packet (codec.get(), nullptr);   // the frames the decoder still holds
	if (! drain (*codec, *frame, channels))
		return false;

	const auto length = channels.empty() ? 0 : (int) channels[0].size();
	samples.setSize ((int) numChannels, length);
	for (int channel = 0; channel < (int) numChannels; ++channel)
		samples.copyFrom (channel, 0, channels[(size_t) channel].data(), length);
	decoded = true;
	return true;
}

bool NiStemReader::readSamples (int* const* destChannels, int numDestChannels, int startOffsetInDestBuffer,
								juce::int64 startSampleInFile, int numSamples)
{
	const bool ok = decodeOnce();
	const auto available = (juce::int64) samples.getNumSamples();

	for (int channel = 0; channel < numDestChannels; ++channel)
	{
		if (destChannels[channel] == nullptr)
			continue;
		auto* dest = reinterpret_cast<float*> (destChannels[channel]) + startOffsetInDestBuffer;
		const auto* source = channel < samples.getNumChannels() ? samples.getReadPointer (channel) : nullptr;
		for (int i = 0; i < numSamples; ++i)
		{
			const auto at = startSampleInFile + i;
			dest[i] = source != nullptr && at >= 0 && at < available ? source[at] : 0.0f;
		}
	}
	return ok;
}
