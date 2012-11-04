/*
  Copyright (c) 2012 by Procera Networks, Inc. ("PROCERA")

  Permission to use, copy, modify, and/or distribute this software for
  any purpose with or without fee is hereby granted, provided that the
  above copyright notice and this permission notice appear in all
  copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND PROCERA DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL PROCERA BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
  OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
#pragma once

#include "stringutil.hpp"

namespace koi {
	enum ServiceState {
		Svc_Failed,
		Svc_Failing,
		Svc_Stopped,
		Svc_Stopping,
		Svc_Starting,
		Svc_Started,
		Svc_Demoting,
		Svc_Promoting,
		Svc_Promoted
	};

	enum ServiceAction {
		Svc_Fail,
		Svc_Stop,
		Svc_Start,
		Svc_Demote,
		Svc_Promote
	};

	inline const char* service_state_string(ServiceState state) {
		static const char* mate[] = {
			"Failed", "Failing", "Stopped", "Stopping", "Starting", "Started", "Demoting", "Promoting", "Promoted"
		};
		return mate[state];
	}

	inline const char* service_action_string(ServiceAction action) {
		static const char* mate[] = {
			"Fail", "Stop", "Start", "Demote", "Promote"
		};
		return mate[action];
	}

	inline ServiceAction service_action_from_string(const string& str) {
		for (int i = 0; i <= (int)Svc_Promote; ++i)
			if (str == service_action_string((ServiceAction)i))
				return (ServiceAction)i;
		return Svc_Fail;
	}

	inline ServiceState service_state_from_string(const string& str) {
		for (int i = 0; i <= (int)Svc_Promoted; ++i)
			if (str == service_state_string((ServiceState)i))
				return (ServiceState)i;
		return Svc_Failed;
	}

	inline int validate_service_action(int action) {
		return (action < 0 || action > Svc_Promote) ? -1 : action;
	}

	inline int validate_service_state(int state) {
		return (state < 0 || state > Svc_Promoted) ? -1 : state;
	}

	struct service_info {
		string _name;
		string _event; // currently executing event
		ServiceState _state;
		bool _failed;

		service_info() : _name(), _event(), _state(Svc_Stopped), _failed(false) {}
		service_info(const char* name, const char* event, ServiceState state, bool failed) :
			_name(name), _event(event), _state(state), _failed(failed) {
		}

		string to_string() const {
			std::stringstream ss;
			ss << _name << ":" << service_state_string(_state) << ":" << _event
			   << (_failed ? ":-" : ":+");
			return ss.str();
		}
	};

	inline service_info parse_service_info(const string& str) {
		service_info inf;
		stringvec sv = split(str, ":");
		if (sv.size() == 4) {
			inf._name = sv[0];
			inf._state = service_state_from_string(sv[1]);
			inf._event = sv[2];
			inf._failed = sv[3] == "-";
		}
		else {
			inf._name = str;
		}
		return inf;
	}
}
