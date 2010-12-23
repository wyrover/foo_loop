
#include <stdafx.h>
#include "looping.h"


namespace loop_helper {
	void console_complain_looping(const char * what, const char * msg) {
		console_looping_formatter() << what << ": " << msg;
	}

	void open_path_helper(input_decoder::ptr & p_input, file::ptr p_file, const char * path,abort_callback & p_abort,bool p_from_redirect,bool p_skip_hints) {
		p_abort.check();
		p_input.release();

		TRACK_CODE("input_entry::g_open_for_decoding",
			input_entry::g_open_for_decoding(p_input,p_file,path,p_abort,p_from_redirect)
			);

		
		if (!p_skip_hints) {
			try {
				static_api_ptr_t<metadb_io>()->hint_reader(p_input.get_ptr(),path,p_abort);
			} catch(exception_io_data) {
				//Don't fail to decode when this barfs, might be barfing when reading info from another subsong than the one we're trying to decode etc.
				p_input.release();
				if (p_file.is_valid()) p_file->reopen(p_abort);
				TRACK_CODE("input_entry::g_open_for_decoding",
					input_entry::g_open_for_decoding(p_input,p_file,path,p_abort,p_from_redirect)
					);
			}
		}
	}

	bool parse_entity(const char * & ptr,pfc::string8 & name,pfc::string8 & value) {
		char delimiter = '\0';
		char tmp;
		t_size n = 0;
		while(isspace((unsigned char) *ptr)) ptr++;
		while(tmp = ptr[n], tmp && !isspace((unsigned char) tmp) && tmp != '=') n++;
		if (!ptr[n]) return false;
		name.set_string(ptr, n);
		ptr += n;
		while(isspace((unsigned char) *ptr)) ptr++;
		if (*ptr != '=') return false;
		ptr++;
		// check delimiter
		if (*ptr == '\'' || *ptr == '"') {
			delimiter = *ptr;
			ptr++;
		}
		if (!*ptr) false;

		n = 0;
		if (delimiter == '\0') {
			while(tmp = ptr[n], tmp && !isspace((unsigned char) tmp) && tmp != ';') n++;
		} else {
			while(tmp = ptr[n], tmp && tmp != delimiter) n++;
		}
		if (!ptr[n]) return false;
		value.set_string(ptr, n);
		ptr += n;
		if (*ptr == delimiter) ptr++;
		while(*ptr == ';' || isspace((unsigned char) *ptr)) ptr++;
		return true;
	}

	t_filestats merge_filestats(const t_filestats & p_src1, const t_filestats & p_src2, int p_merge_type) {
		t_filestats dest;
		dest.m_timestamp = pfc::max_t(p_src1.m_timestamp, p_src2.m_timestamp);
		if (p_merge_type == merge_filestats_sum) {
			dest.m_size = p_src1.m_size + p_src2.m_size;
		} else if (p_merge_type == merge_filestats_max) {
			dest.m_size = pfc::max_t(p_src1.m_size, p_src2.m_size);
		} else {
			throw pfc::exception_not_implemented();
		}
		return dest;
	}

	// {E50D5DE0-6F95-4b1c-9165-63D80415ED1B}
	FOOGUIDDECL const GUID loop_type_base::guid_cfg_branch_loop = 
	{ 0xe50d5de0, 0x6f95, 0x4b1c, { 0x91, 0x65, 0x63, 0xd8, 0x4, 0x15, 0xed, 0x1b } };
	// {69B1AEBB-E2A6-4c73-AEA2-5037A79B1B62}
	static const GUID guid_cfg_loop_debug = 
	{ 0x69b1aebb, 0xe2a6, 0x4c73, { 0xae, 0xa2, 0x50, 0x37, 0xa7, 0x9b, 0x1b, 0x62 } };
	// {9208BA62-AFBE-450e-A468-72792DAE5193}
	static const GUID guid_cfg_loop_disable = 
	{ 0x9208ba62, 0xafbe, 0x450e, { 0xa4, 0x68, 0x72, 0x79, 0x2d, 0xae, 0x51, 0x93 } };

	static advconfig_branch_factory cfg_loop_branch("Looping",loop_type_base::guid_cfg_branch_loop,advconfig_branch::guid_branch_decoding,0);
	advconfig_checkbox_factory cfg_loop_debug("Loop Debugging",guid_cfg_loop_debug,loop_type_base::guid_cfg_branch_loop,0,false);
	advconfig_checkbox_factory cfg_loop_disable("Loop Disable",guid_cfg_loop_disable,loop_type_base::guid_cfg_branch_loop,0,false);

}


// some looping type class
static loop_type_factory_t<loop_type_none> g_loop_type_none;

class loop_type_loopstartlength : public loop_type_impl_singleinput_base
{
private:
	input_decoder::ptr m_input;
	loop_event_point_list m_points;
public:
	static const char * g_get_name() {return "LoopStart/LoopLength";}
	static const char * g_get_short_name() {return "loopstartlength";}
	static bool g_is_our_type(const char * type) {return !pfc::stringCompareCaseInsensitive(type, "loopstartlength");}
	static bool g_is_explicit() {return false;}
	virtual bool parse(const char * ptr) {
		return true;
	}
	virtual bool open_path_internal(file::ptr p_filehint,const char * path,t_input_open_reason p_reason,abort_callback & p_abort,bool p_from_redirect,bool p_skip_hints) {
		if (p_reason == input_open_info_write) throw exception_io_unsupported_format();//our input does not support retagging.
		try {
			open_path_helper(m_input, p_filehint, path, p_abort, p_from_redirect,p_skip_hints);
		} catch (exception_io_not_found) {
			return false;
		}
		switch_input(m_input);
		file_info_impl p_info;
		get_input()->get_info(0, p_info, p_abort);
		m_points.remove_all();
		if (p_info.meta_get_count_by_name("LOOPSTART") != 1 ||
			p_info.meta_get_count_by_name("LOOPLENGTH") != 1)
			return false;
		loop_event_point_simple * point = new service_impl_t<loop_event_point_simple>();
		t_uint64 start = pfc::atoui64_ex(p_info.meta_get("LOOPSTART", 0), ~0);
		t_uint64 length = pfc::atoui64_ex(p_info.meta_get("LOOPLENGTH", 0), ~0);
		point->from = start + length;
		point->to = start;
		m_points.add_item(point);
		switch_points(m_points);
		return true;
	}
	virtual void get_info(t_uint32 subsong, file_info & p_info,abort_callback & p_abort) {
		get_input()->get_info(subsong, p_info, p_abort);
		get_info_for_points(p_info, m_points, get_info_prefix(), get_sample_rate());
	}
};

static loop_type_factory_t<loop_type_loopstartlength> g_loop_type_loopstartlength;

class loop_event_point_twofiles_eof : public loop_event_point_baseimpl {
public:
	// situation determined on loop type
	loop_event_point_twofiles_eof() : loop_event_point_baseimpl(on_looping | on_no_looping) {}
	virtual t_uint64 get_position() const {return (t_uint64)-1;}
	virtual t_uint64 get_prepare_position() const {return (t_uint64)-1;}
	virtual void check() const {}
	virtual void get_info(file_info & p_info, const char * p_prefix, t_uint32 sample_rate) {}
	virtual bool process(loop_type_base::ptr p_input, t_uint64 p_start, audio_chunk & p_chunk, mem_block_container * p_raw, abort_callback & p_abort) {
		// no truncate (because EOF)
		return process(p_input, p_abort);
	}
	virtual bool process(loop_type_base::ptr p_input, abort_callback & p_abort);
};

static struct {char * head_suffix; char * body_suffix; } const g_known_suffix_table[] =
{
	{"_A", "_B"},
	{"_head", "_body"},
	{"_head", "_loop"},
	{"a", "b"},
};

class loop_type_twofiles : public loop_type_impl_base
{
private:
	struct somefile {
		input_decoder::ptr input;
		pfc::string8 suffix;
		pfc::string8 path;
		t_uint64 samples;
		bool samples_known;
	};
	loop_event_point_list m_points;
	somefile * m_current;
	somefile m_head, m_body;
	pfc::string8 m_current_fileext;
	bool m_current_changed;
	bool m_autoprobe;

	inline void switch_to(somefile & newfile) {
		if (m_current != &newfile) {
			m_current_changed = true;
		}
		m_current = &newfile;
		switch_input(newfile.input);
		m_current_fileext = pfc::string_filename_ext(newfile.path);
	}

protected:
	void virtual open_decoding_internal(t_uint32 subsong, t_uint32 flags, abort_callback & p_abort) {
		m_head.input->initialize(subsong, flags, p_abort);
		m_body.input->initialize(subsong, flags, p_abort);		
	}
	virtual bool set_dynamic_info_track(file_info & p_out) {
		bool ret = loop_type_impl_base::set_dynamic_info_track(p_out);
		if (m_current_changed) {
			p_out.info_set("loop_current_file", m_current_fileext);
			return true;
		} else {
			return ret;
		}
	}
	
	virtual bool reset_dynamic_info_track(file_info & p_out) {
		return loop_type_impl_base::reset_dynamic_info_track(p_out) |
			p_out.info_remove("loop_current_file");
	}
public:
	static const char * g_get_name() {return "Two Files (head and body)";}
	static const char * g_get_short_name() {return "twofiles";}
	static bool g_is_our_type(const char * type) {return !pfc::stringCompareCaseInsensitive(type, "twofiles");}
	static bool g_is_explicit() {return false;}
	virtual bool parse(const char * ptr) {
		pfc::string8 name, value;
		m_autoprobe = true;
		while (parse_entity(ptr, name, value)) {
			if (!pfc::stringCompareCaseInsensitive(name, "head-suffix")) {
				m_head.suffix = value;
				m_autoprobe = false;
			} else if (!pfc::stringCompareCaseInsensitive(name, "body-suffix")) {
				m_body.suffix = value;
				m_autoprobe = false;
			} else {
				// ignore unknown entities
				//return false;
			}
		}
		if (!m_autoprobe && m_head.suffix == m_body.suffix) return false;
		return true;
	}

	virtual bool open_path_internal(file::ptr p_filehint,const char * p_path,t_input_open_reason p_reason,abort_callback & p_abort,bool p_from_redirect,bool p_skip_hints) {
		if (p_reason == input_open_info_write) throw exception_io_unsupported_format();//our input does not support retagging.
		pfc::string8 base, ext;
		base = p_path;
		t_size extpos = base.find_last('.');
		if (extpos >= base.scan_filename()) {
			// is ext
			ext.set_string(base + extpos);
			base.truncate(extpos);
		} else {
			ext = "";
		}
		if (m_autoprobe) {
			// try some known suffixes
			bool found = false;
			unsigned n;
			pfc::string8 work(base);
			t_size baselen = base.get_length();
			for(n=0;n<tabsize(g_known_suffix_table);n++)
			{
				work.truncate(baselen);
				work << g_known_suffix_table[n].head_suffix << ext;
				if (!filesystem::g_exists(work, p_abort)) continue;
				work.truncate(baselen);
				work << g_known_suffix_table[n].body_suffix << ext;
				if (!filesystem::g_exists(work, p_abort)) continue;
				m_head.suffix = g_known_suffix_table[n].head_suffix;
				m_body.suffix = g_known_suffix_table[n].body_suffix;
				found = true;
				if (cfg_loop_debug.get())
					console_looping_debug_formatter() << "twofiles: auto-probe: head-suffix=" << m_head.suffix << " body-suffix=" << m_body.suffix;
				break;
			}
			if (!found) return false;
		}
		m_head.path.reset();
		m_head.path << base << m_head.suffix << ext;
		try {
			open_path_helper(m_head.input, NULL, m_head.path, p_abort, p_from_redirect, p_skip_hints);
		} catch (exception_io_not_found) {
			return false;
		}
		m_body.path.reset();
		m_body.path << base << m_body.suffix << ext;
		try {
			open_path_helper(m_body.input, NULL, m_body.path, p_abort, p_from_redirect, p_skip_hints);
		} catch (exception_io_not_found) {
			console::formatter() << "loop twofiles: body file not found: \"" << file_path_display(m_body.path) << "\"";
			return false;
		}
		if (m_head.input.is_empty() || m_body.input.is_empty()) return false;
		m_points.remove_all();
		loop_event_point_twofiles_eof * point = new service_impl_t<loop_event_point_twofiles_eof>();
		m_points.add_item(point);
		switch_points(m_points);
		file_info_impl p_info;
		m_head.input->get_info(0, p_info, p_abort);
		m_head.samples = p_info.info_get_length_samples();
		p_info.reset();
		m_body.input->get_info(0, p_info, p_abort);
		m_body.samples = p_info.info_get_length_samples();
		switch_to(m_head);
		return true;
	}
	virtual t_uint32 get_subsong_count() {
		return 1;
	}
	virtual t_uint32 get_subsong(t_uint32 p_index) {
		assert(p_index == 0);
		return 0;
	}
	virtual void get_info(t_uint32 subsong, file_info & p_info,abort_callback & p_abort) {
		t_uint32 sample_rate = get_sample_rate();
		m_head.input->get_info(subsong, p_info, p_abort);
		pfc::string8 name;
		name << get_info_prefix();
		t_size prefixlen = name.get_length();
		name << "head_length";
		p_info.info_set(name, format_samples_ex(m_head.samples, sample_rate));
		name.truncate(prefixlen);
		name << "body_length";
		p_info.info_set(name, format_samples_ex(m_body.samples, sample_rate));
		p_info.set_length(audio_math::samples_to_time(m_head.samples + m_body.samples, sample_rate));
		if (m_autoprobe) {
			name.truncate(prefixlen);
			name << "autoprobe";
			pfc::string8 value;
			value << "type=" << g_get_short_name() << " head-suffix=" << m_head.suffix << " body-suffix=" << m_body.suffix;
			p_info.info_set(name, value);
		}
	}
	virtual t_filestats get_file_stats(abort_callback & p_abort) {
		return merge_filestats(
			m_head.input->get_file_stats(p_abort),
			m_body.input->get_file_stats(p_abort),
			merge_filestats_sum);
	}
	void virtual close() {
		m_head.input.release();
		m_body.input.release();
	}
	void virtual on_idle(abort_callback & p_abort) {
		m_head.input->on_idle(p_abort);
		m_body.input->on_idle(p_abort);
	}
	virtual bool on_eof_event(abort_callback & p_abort) {
		if (get_no_looping() && m_current == &m_body)
			return false;
		switch_to(m_body);
		raw_seek((t_uint64)0,p_abort);
		return true;
	}
	virtual void seek(double p_seconds,abort_callback & p_abort) {
		seek(audio_math::time_to_samples(p_seconds, get_sample_rate()),p_abort);
	}
	virtual void seek(t_uint64 p_samples,abort_callback & p_abort) {
		if (p_samples < m_head.samples) {
			switch_to(m_head);
			user_seek(p_samples,p_abort);
		} else {
			p_samples -= m_head.samples;
			switch_to(m_body);
			user_seek(p_samples,p_abort);
		}
	}
	FB2K_MAKE_SERVICE_INTERFACE(loop_type_twofiles, loop_type);
};

bool loop_event_point_twofiles_eof::process(loop_type_base::ptr p_input, abort_callback & p_abort) {
	loop_type_twofiles::ptr p_input_special;
	if (p_input->service_query_t<loop_type_twofiles>(p_input_special)) {
		return p_input_special->on_eof_event(p_abort);
	}
	return false;
}

static loop_type_factory_t<loop_type_twofiles> g_loop_type_twofiles;

class loop_type_sampler : public loop_type_impl_singleinput_base
{
private:
	input_decoder::ptr m_input;
	loop_event_point_list m_points;
public:
	static const char * g_get_name() {return "Wave(RIFF) Sampler";}
	static const char * g_get_short_name() {return "sampler";}
	static bool g_is_our_type(const char * type) {return !pfc::stringCompareCaseInsensitive(type, "sampler");}
	static bool g_is_explicit() {return false;}
	virtual bool parse(const char * ptr) {
		return true;
	}
	virtual bool open_path_internal(file::ptr p_filehint,const char * path,t_input_open_reason p_reason,abort_callback & p_abort,bool p_from_redirect,bool p_skip_hints) {
		if (p_filehint.is_empty()) {
			try {
				filesystem::g_open_read(p_filehint,path,p_abort);
			} catch (exception_io_not_found) {
				return false;
			}
		}
		p_filehint->reopen(p_abort);
		if (p_filehint->is_remote()) {
			console::formatter() << "loop sampler: file must be local: \"" << file_path_display(path) << "\"";
			return false;
		}
		pfc::string8 buf;
		t_uint32 size;
		p_filehint->read_string_ex(buf, 4, p_abort); // chunkname: RIFF
		if (!!pfc::strcmp_ex(buf, 4, "RIFF", 4))
			return false;
		p_filehint->read_lendian_t(size, p_abort); // chunksize
		p_filehint->read_string_ex(buf, 4, p_abort); // filetype: WAVE
		while (!p_filehint->is_eof(p_abort)) {
			p_filehint->read_string_ex(buf, 4, p_abort); // chunkname
			p_filehint->read_lendian_t(size, p_abort); // chunksize
			if (!!pfc::strcmp_ex(buf, 4, "smpl", 4)) {
				p_filehint->skip(size, p_abort);
				continue;
			}
			stream_reader_limited_ref chunk_reader(p_filehint.get_ptr(), size);
			// chunk found;
/*
        UnsignedInt('manufacturer'),
        UnsignedInt('product'),
        UnsignedInt('sample_period'),
        UnsignedInt('midi_unity_note'),
        UnsignedInt('midi_pitch_fraction'),
        UnsignedInt('smpte_format'),
        UnsignedInt('smpte_offset'),
        UnsignedInt('num_sample_loops'),
        UnsignedInt('sampler_data'),
*/
			t_uint32 i;
			chunk_reader.skip(4 * 7, p_abort);
			chunk_reader.read_lendian_t(i, p_abort);
			chunk_reader.skip(4, p_abort); // sampler_data
			for (; i>0; i--) {
/*
        UnsignedInt('cue_point_id'),
        UnsignedInt('type'),
        UnsignedInt('start'),
        UnsignedInt('end'),
        UnsignedInt('fraction'),
        UnsignedInt('playcount'),
*/
				loop_event_point_simple * point = new service_impl_t<loop_event_point_simple>();
				t_uint32 temp;
				chunk_reader.read_lendian_t(temp, p_abort); // cue_point_id: ignore
				chunk_reader.read_lendian_t(temp, p_abort); // type: currently known: 0 only
				if (temp != 0) {
					pfc::string8 errmsg;
					errmsg << "Unknown sampleloop type: " << temp;
					throw exception_io_data(errmsg);
				}
				chunk_reader.read_lendian_t(temp, p_abort); // start
				point->to = temp;
				chunk_reader.read_lendian_t(temp, p_abort); // end
				point->from = temp;
				chunk_reader.read_lendian_t(temp, p_abort); // fraction
				if (temp != 0) {
					pfc::string8 errmsg;
					errmsg << "Unknown sampleloop fraction: " << temp;
					throw exception_io_data(errmsg);
				}
				chunk_reader.read_lendian_t(temp, p_abort); // playcount
				point->maxrepeats = temp;
				m_points.add_item(point);
			}
		}
		if (m_points.get_count() == 0)
			return false;
		try {
			open_path_helper(m_input, p_filehint, path, p_abort, p_from_redirect,p_skip_hints);
		} catch (exception_io_not_found) {
			return false;
		}
		switch_input(m_input);
		switch_points(m_points);
		return true;
	}
	virtual void get_info(t_uint32 subsong, file_info & p_info,abort_callback & p_abort) {
		get_input()->get_info(subsong, p_info, p_abort);
		get_info_for_points(p_info, m_points, get_info_prefix(), get_sample_rate());
	}
};

static loop_type_factory_t<loop_type_sampler> g_loop_type_sampler;

#pragma region GUIDs
// {C9E7AF50-FDF8-4a2f-99A6-8DE4D2B49D0C}
FOOGUIDDECL const GUID loop_type::class_guid = 
{ 0xc9e7af50, 0xfdf8, 0x4a2f, { 0x99, 0xa6, 0x8d, 0xe4, 0xd2, 0xb4, 0x9d, 0xc } };

// {CA8E32C1-1A2D-4679-87AB-03292A97D890}
FOOGUIDDECL const GUID loop_type_base::class_guid = 
{ 0xca8e32c1, 0x1a2d, 0x4679, { 0x87, 0xab, 0x3, 0x29, 0x2a, 0x97, 0xd8, 0x90 } };

// {2910A6A6-A12B-414f-971B-90A65F79439B}
FOOGUIDDECL const GUID loop_event_point::class_guid = 
{ 0x2910a6a6, 0xa12b, 0x414f, { 0x97, 0x1b, 0x90, 0xa6, 0x5f, 0x79, 0x43, 0x9b } };

//// {CFBEBA19-9B94-46b0-AA21-D2906B139EDE}
FOOGUIDDECL const GUID loop_type_twofiles::class_guid = 
{ 0xcfbeba19, 0x9b94, 0x46b0, { 0xaa, 0x21, 0xd2, 0x90, 0x6b, 0x13, 0x9e, 0xde } };

//// {566BCC79-7370-48c0-A7CB-5E47C4C17A86}
FOOGUIDDECL const GUID loop_type_entry::class_guid = 
{ 0x566bcc79, 0x7370, 0x48c0, { 0xa7, 0xcb, 0x5e, 0x47, 0xc4, 0xc1, 0x7a, 0x86 } };

#pragma endregion

