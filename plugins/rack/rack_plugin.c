#include "uwsgi_rack.h"

extern struct uwsgi_server uwsgi;

struct uwsgi_rack ur;

struct option uwsgi_rack_options[] = {

        {"rails", required_argument, 0, LONG_ARGS_RAILS},
        {"rack", required_argument, 0, LONG_ARGS_RACK},
        {"ruby-gc-freq", required_argument, 0, LONG_ARGS_RUBY_GC_FREQ},
        {"rb-gc-freq", required_argument, 0, LONG_ARGS_RUBY_GC_FREQ},
        {"rb-require", required_argument, 0, LONG_ARGS_RUBY_REQUIRE},
        {"ruby-require", required_argument, 0, LONG_ARGS_RUBY_REQUIRE},
        {"rbrequire", required_argument, 0, LONG_ARGS_RUBY_REQUIRE},
        {"rubyrequire", required_argument, 0, LONG_ARGS_RUBY_REQUIRE},
        {"require", required_argument, 0, LONG_ARGS_RUBY_REQUIRE},
        {"rbshell", optional_argument, 0, LONG_ARGS_RUBY_SHELL},

        {0, 0, 0, 0},

};

void uwsgi_ruby_exception(void) {

        VALUE lasterr = rb_gv_get("$!");
        VALUE message = rb_obj_as_string(lasterr);

        uwsgi_log("%s\n", RSTRING_PTR(message));
        if(!NIL_P(rb_errinfo())) {
                VALUE ary = rb_funcall(rb_errinfo(), rb_intern("backtrace"), 0);
                int i;
                for (i=0; i<RARRAY_LEN(ary); i++) {
                        uwsgi_log("%s\n", RSTRING_PTR(RARRAY_PTR(ary)[i]));
                }
        }
}




extern struct http_status_codes hsc[];


VALUE rb_uwsgi_io_new(VALUE class, VALUE wr) {

	struct wsgi_request *wsgi_req;
	Data_Get_Struct(wr, struct wsgi_request, wsgi_req);
	VALUE self = Data_Wrap_Struct(class , 0, 0, wsgi_req);

	rb_obj_call_init(self, 0, NULL);

	return self;

}

VALUE rb_uwsgi_io_init(int argc, VALUE *argv, VALUE self) {

	return self;
}

VALUE rb_uwsgi_io_gets(VALUE obj, VALUE args) {

	struct wsgi_request *wsgi_req;
	Data_Get_Struct(obj, struct wsgi_request, wsgi_req);

	// return the whole body as string

	uwsgi_log("rack.input gets is not implemented (req %p)\n", wsgi_req);
	return Qnil;
}

VALUE rb_uwsgi_io_each(VALUE obj, VALUE args) {

	struct wsgi_request *wsgi_req;
	Data_Get_Struct(obj, struct wsgi_request, wsgi_req);

	// yield strings chunks
	uwsgi_log("rack.input each is not implemented (req %p)\n", wsgi_req);

	return Qnil;
}

VALUE rb_uwsgi_io_read(VALUE obj, VALUE args) {

	struct wsgi_request *wsgi_req;
	Data_Get_Struct(obj, struct wsgi_request, wsgi_req);
	VALUE chunk;
	int chunk_size;


	if (!wsgi_req->post_cl || wsgi_req->buf_pos >= wsgi_req->post_cl) {
		return Qnil;
	}
	
	if (RARRAY_LEN(args) == 0) {
		chunk = rb_str_new(wsgi_req->post_buffering_buf+wsgi_req->buf_pos, wsgi_req->post_cl-wsgi_req->buf_pos);
		wsgi_req->buf_pos += (wsgi_req->post_cl-wsgi_req->buf_pos);
		return chunk;
	}
	else if (RARRAY_LEN(args) > 0) {
		chunk_size = NUM2INT(RARRAY_PTR(args)[0]);
		if (wsgi_req->buf_pos+chunk_size > wsgi_req->post_cl) {
			chunk_size = wsgi_req->post_cl-wsgi_req->buf_pos;
		}
		if (RARRAY_LEN(args) > 1) {
			rb_str_cat(RARRAY_PTR(args)[1], wsgi_req->post_buffering_buf+wsgi_req->buf_pos, chunk_size);
			wsgi_req->buf_pos+=chunk_size;
			return RARRAY_PTR(args)[1];
		}
		wsgi_req->buf_pos+=chunk_size;
		return rb_str_new(wsgi_req->post_buffering_buf+wsgi_req->buf_pos, chunk_size);
	}

	return Qnil;
}

VALUE rb_uwsgi_io_rewind(VALUE obj, VALUE args) {

	struct wsgi_request *wsgi_req;
	Data_Get_Struct(obj, struct wsgi_request, wsgi_req);

	if (!wsgi_req->post_cl) {
		return Qnil;
	}

	wsgi_req->buf_pos = 0;

	return Qnil;
}

#ifdef RUBY19
RUBY_GLOBAL_SETUP
#endif

VALUE uwsgi_require_file(VALUE arg) {
    return rb_funcall(rb_cObject, rb_intern("require"), 1, arg);
}

VALUE require_rack(VALUE arg) {
    return rb_funcall(rb_cObject, rb_intern("require"), 1, rb_str_new2("rack"));
}

VALUE require_rails(VALUE arg) {
#ifdef RUBY19
    return rb_require("./config/environment");
#else
    return rb_require("config/environment");
#endif
}

VALUE init_rack_app(VALUE);

VALUE rack_call_rpc_handler(VALUE args) {
        VALUE rpc_args = rb_ary_entry(args, 1);
        return rb_funcall2(rb_ary_entry(args, 0), rb_intern("call"), RARRAY_LEN(rpc_args), RARRAY_PTR(rpc_args));
}


uint16_t uwsgi_ruby_rpc(void *func, uint8_t argc, char **argv, char *buffer) {

        uint8_t i;
	VALUE rb_args = rb_ary_new2(2);
        VALUE rb_rpc_args = rb_ary_new2(argc);
        VALUE ret;
	int error = 0;
        char *rv;
        size_t rl;

	rb_ary_store(rb_args, 0, (VALUE) func);

        for (i = 0; i < argc; i++) {
                rb_ary_store(rb_rpc_args, i, rb_str_new2(argv[i]));
        }
	rb_ary_store(rb_args, 1, rb_rpc_args);


	ret = rb_protect(rack_call_rpc_handler, rb_args, &error);

        if (error) {
		uwsgi_ruby_exception();
		return 0;
	}

	if (TYPE(ret) == T_STRING) {
        	rv = RSTRING_PTR(ret);
                rl = RSTRING_LEN(ret);
                if (rl <= 0xffff) {
                	memcpy(buffer, rv, rl);
                        return rl;
                }
        }

        return 0;

}


int uwsgi_rack_init(){

	struct http_status_codes *http_sc;
#ifdef RUBY19
	int argc = 2;
	char *sargv[] = { (char *) "uwsgi", (char *) "-e0" };
	char **argv = sargv;
#endif


	// filling http status codes
        for (http_sc = hsc; http_sc->message != NULL; http_sc++) {
                http_sc->message_size = (int) strlen(http_sc->message);
        }

	ur.unprotected = 0;

#ifdef RUBY19
	ruby_sysinit(&argc, &argv);
	RUBY_INIT_STACK
	ruby_init();
	ruby_process_options(argc, argv);
#else
	ruby_init();
	ruby_init_loadpath();
#endif
	ruby_show_version();

	ruby_script("uwsgi");

	ur.signals_protector = rb_ary_new();
	ur.rpc_protector = rb_ary_new();
	rb_gc_register_address(&ur.signals_protector);
	rb_gc_register_address(&ur.rpc_protector);

#ifdef UWSGI_EMBEDDED
	uwsgi_rack_init_api();	
#endif

	return 0;
}

void uwsgi_rack_init_apps(void) {

	int error;
	ur.app_id = uwsgi_apps_cnt;
	struct uwsgi_string_list *usl = ur.rbrequire;

	while(usl) {
		error = 0;
		rb_protect( uwsgi_require_file, rb_str_new2(usl->value), &error ) ;
                if (error) {
                        uwsgi_ruby_exception();
		}
		usl = usl->next;
	}

	if (ur.rack) {
		ur.dispatcher = rb_protect(init_rack_app, rb_str_new2(ur.rack), &error);
		if (error) {
                        uwsgi_ruby_exception();
                        exit(1);
                }
		if (ur.dispatcher == Qnil) {
			uwsgi_log("unable to find RACK entry point\n");
			exit(1);
		}
		rb_gc_register_address(&ur.dispatcher);

		goto ready;
	}
	else if (ur.rails) {
		if (chdir(ur.rails)) {
			uwsgi_error("chdir()");
			exit(1);
		}

		uwsgi_log("loading rails app %s\n", ur.rails);
		rb_protect( require_rails, 0, &error ) ;
		if (error) {
                	uwsgi_ruby_exception();
			exit(1);
                }
		uwsgi_log("rails app %s ready\n", ur.rails);
		VALUE ac = rb_const_get(rb_cObject, rb_intern("ActionController"));

		ur.dispatcher = rb_funcall( rb_const_get(ac, rb_intern("Dispatcher")), rb_intern("new"), 0);

		if (ur.dispatcher == Qnil) {
			uwsgi_log("unable to load rails dispatcher\n");
			exit(1);
		}

		goto ready;
	}

	return;

ready:
	ur.call = rb_intern("call");
	if (!ur.call) {
		uwsgi_log("unable to find RACK entry point\n");
		return;
	}
	rb_gc_register_address(&ur.call);


	ur.rb_uwsgi_io_class = rb_define_class("Uwsgi_IO", rb_cObject);

	rb_gc_register_address(&ur.rb_uwsgi_io_class);

	rb_define_singleton_method(ur.rb_uwsgi_io_class, "new", rb_uwsgi_io_new, 1);
	rb_define_method(ur.rb_uwsgi_io_class, "initialize", rb_uwsgi_io_init, -1);
	rb_define_method(ur.rb_uwsgi_io_class, "gets", rb_uwsgi_io_gets, 0);
	rb_define_method(ur.rb_uwsgi_io_class, "each", rb_uwsgi_io_each, 0);
	rb_define_method(ur.rb_uwsgi_io_class, "read", rb_uwsgi_io_read, -2);
	rb_define_method(ur.rb_uwsgi_io_class, "rewind", rb_uwsgi_io_rewind, 0);

	uwsgi_add_app(ur.app_id, 7, "", 0);
	if (ur.gc_freq <= 1) {
        	uwsgi_log("RACK app %d loaded at %p (GC frequency: AGGRESSIVE)\n", ur.app_id, ur.call);
	}
	else {
        	uwsgi_log("RACK app %d loaded at %p (GC frequency: %d)\n", ur.app_id, ur.call, ur.gc_freq);
	}

}

VALUE call_dispatch(VALUE env) {

	return rb_funcall(ur.dispatcher, ur.call, 1, env);

}

VALUE send_body(VALUE obj) {

	struct wsgi_request *wsgi_req = current_wsgi_req();
	ssize_t len = 0;

	//uwsgi_log("sending body\n");
	if (TYPE(obj) == T_STRING) {
		len = wsgi_req->socket->proto_write( wsgi_req, RSTRING_PTR(obj), RSTRING_LEN(obj));
	}
	else {
		uwsgi_log("UNMANAGED BODY TYPE %d\n", TYPE(obj));
	}

	wsgi_req->response_size += len;

	return Qnil;
}

VALUE iterate_body(VALUE body) {

#ifdef RUBY19
	return rb_block_call(body, rb_intern("each"), 0, 0, send_body, 0);
#else
	return rb_iterate(rb_each, body, send_body, 0);
#endif

}

VALUE send_header(VALUE obj, VALUE headers) {

	struct wsgi_request *wsgi_req = current_wsgi_req();

	size_t len;
	VALUE hkey, hval;

	
	//uwsgi_log("HEADERS %d\n", TYPE(obj));
	if (TYPE(obj) == T_ARRAY) {
		if (RARRAY_LEN(obj) >= 2) {
			hkey = rb_obj_as_string( RARRAY_PTR(obj)[0]);
			hval = rb_obj_as_string( RARRAY_PTR(obj)[1]);

		}
		else {
			goto clear;
		}
	}
	else if (TYPE(obj) == T_STRING) {
		hkey = obj;
#ifdef RUBY19
		hval = rb_hash_lookup(headers, obj);
#else
		hval = rb_hash_aref(headers, obj);
#endif
	}
	else {
		goto clear;
	}

	if (TYPE(hkey) != T_STRING || TYPE(hval) != T_STRING) {
		goto clear2;
	}

	//uwsgi_log("header: %.*s: %.*s\n", RSTRING_LEN(hkey), RSTRING_PTR(hkey), RSTRING_LEN(hval), RSTRING_PTR(hval));

	len = wsgi_req->socket->proto_write_header( wsgi_req, RSTRING_PTR(hkey), RSTRING_LEN(hkey));
	wsgi_req->headers_size += len;
	len = wsgi_req->socket->proto_write_header( wsgi_req, ": ", 2);
	wsgi_req->headers_size += len;

	char *header_value = RSTRING_PTR(hval);
	int header_value_len = RSTRING_LEN(hval);

	char *header_value_splitted = memchr(header_value, '\n', header_value_len);

	if (!header_value_splitted) {
		len = wsgi_req->socket->proto_write_header( wsgi_req, header_value, header_value_len);
		wsgi_req->headers_size += len;
		len = wsgi_req->socket->proto_write_header( wsgi_req, "\r\n", 2);
		wsgi_req->headers_size += len;
		wsgi_req->header_cnt++;
	}
	else {
		header_value_splitted[0] = 0;
		len = wsgi_req->socket->proto_write_header( wsgi_req, header_value, header_value_splitted-header_value);
		wsgi_req->headers_size += len;
		len = wsgi_req->socket->proto_write_header( wsgi_req, "\r\n", 2);
                wsgi_req->headers_size += len;
		wsgi_req->header_cnt++;

		header_value = header_value_splitted+1;
		header_value_len -= header_value_splitted-header_value;

		while(header_value_len && (header_value_splitted = memchr(header_value, '\n', header_value_len))) {
			header_value_splitted[0] = 0;

			len = wsgi_req->socket->proto_write( wsgi_req, RSTRING_PTR(hkey), RSTRING_LEN(hkey));
        		wsgi_req->headers_size += len;
        		len = wsgi_req->socket->proto_write( wsgi_req, ": ", 2);
        		wsgi_req->headers_size += len;

			len = wsgi_req->socket->proto_write( wsgi_req, header_value, header_value_splitted-header_value);
			wsgi_req->headers_size += len;
			len = wsgi_req->socket->proto_write( wsgi_req, "\r\n", 2);
                	wsgi_req->headers_size += len;		
                	wsgi_req->header_cnt++;

                	header_value = header_value_splitted+1;
                	header_value_len -= header_value_splitted-header_value;	
		}
	}


clear2:
	rb_gc_unregister_address(&hkey);
	rb_gc_unregister_address(&hval);

clear:

	return Qnil;
}

VALUE iterate_headers(VALUE headers) {

#ifdef RUBY19
        return rb_block_call(headers, rb_intern("each"), 0, 0, send_header, headers );
#else
        return rb_iterate(rb_each, headers, send_header, headers);
#endif

}



int uwsgi_rack_request(struct wsgi_request *wsgi_req) {

	int error = 0;
	int i;
	VALUE env, ret, status, headers, body;

	struct http_status_codes *http_sc;

	/* Standard RACK request */
        if (!wsgi_req->uh.pktsize) {
                uwsgi_log("Invalid RACK request. skip.\n");
                return -1;
        }

        if (uwsgi_parse_vars(wsgi_req)) {
                return -1;
        }

	wsgi_req->app_id = ur.app_id;
	uwsgi_apps[wsgi_req->app_id].requests++;


        env = rb_hash_new();

        // fill ruby hash
        for(i=0;i<wsgi_req->var_cnt;i++) {

		// put the var only if it is not 0 size or required (rack requirement... very inefficient)
		if (wsgi_req->hvec[i+1].iov_len > 0 || 
					!uwsgi_strncmp((char *)"REQUEST_METHOD", 14, wsgi_req->hvec[i].iov_base, (int) wsgi_req->hvec[i].iov_len) ||
					!uwsgi_strncmp((char *)"SCRIPT_NAME", 11, wsgi_req->hvec[i].iov_base, (int) wsgi_req->hvec[i].iov_len) ||
					!uwsgi_strncmp((char *)"PATH_INFO", 10, wsgi_req->hvec[i].iov_base, (int) wsgi_req->hvec[i].iov_len) ||
					!uwsgi_strncmp((char *)"QUERY_STRING", 12, wsgi_req->hvec[i].iov_base, (int) wsgi_req->hvec[i].iov_len) ||
					!uwsgi_strncmp((char *)"SERVER_NAME", 11, wsgi_req->hvec[i].iov_base, (int) wsgi_req->hvec[i].iov_len) ||
					!uwsgi_strncmp((char *)"SERVER_PORT", 11, wsgi_req->hvec[i].iov_base, (int) wsgi_req->hvec[i].iov_len)
							) {
			rb_hash_aset(env, rb_str_new(wsgi_req->hvec[i].iov_base, wsgi_req->hvec[i].iov_len),
					rb_str_new(wsgi_req->hvec[i+1].iov_base, wsgi_req->hvec[i+1].iov_len));
		}
                i++;
        }


	VALUE rbv = rb_ary_new();
	rb_ary_store(rbv, 0, INT2NUM(1));
	rb_ary_store(rbv, 1, INT2NUM(1));
	rb_hash_aset(env, rb_str_new2("rack.version"), rbv);

	if (wsgi_req->scheme_len > 0) {
		rb_hash_aset(env, rb_str_new2("rack.url_scheme"), rb_str_new(wsgi_req->scheme, wsgi_req->scheme_len));
        }
        else if (wsgi_req->https_len > 0) {
                if (!strncasecmp(wsgi_req->https, "on", 2) || wsgi_req->https[0] == '1') {
			rb_hash_aset(env, rb_str_new2("rack.url_scheme"), rb_str_new2("https"));
                }
                else {
			rb_hash_aset(env, rb_str_new2("rack.url_scheme"), rb_str_new2("http"));
                }
        }
        else {
		rb_hash_aset(env, rb_str_new2("rack.url_scheme"), rb_str_new2("http"));
        }


	rb_hash_aset(env, rb_str_new2("rack.multithread"), Qfalse);
	rb_hash_aset(env, rb_str_new2("rack.multiprocess"), Qtrue);
	rb_hash_aset(env, rb_str_new2("rack.run_once"), Qfalse);

	VALUE dws_wr = Data_Wrap_Struct(ur.rb_uwsgi_io_class, 0, 0, wsgi_req);

	if (wsgi_req->async_post) {
		rb_hash_aset(env, rb_str_new2("rack.input"), rb_funcall( rb_const_get(rb_cObject, rb_intern("IO")), rb_intern("new"), 2, INT2NUM(fileno(wsgi_req->async_post)), rb_str_new("r",1) ));
	}
	else {
		rb_hash_aset(env, rb_str_new2("rack.input"), rb_funcall(ur.rb_uwsgi_io_class, rb_intern("new"), 1, dws_wr ));
	}

	rb_hash_aset(env, rb_str_new2("rack.errors"), rb_funcall( rb_const_get(rb_cObject, rb_intern("IO")), rb_intern("new"), 2, INT2NUM(2), rb_str_new("w",1) ));


	if (ur.unprotected) {
		ret = rb_funcall(ur.dispatcher, ur.call, 1, env);
	}
	else {
		ret = rb_protect( call_dispatch, env, &error);
	}
	
	if (error) {
		uwsgi_ruby_exception();
		//return -1;
	}

	if (TYPE(ret) == T_ARRAY) {
		if (RARRAY_LEN(ret) != 3) {
			uwsgi_log("Invalid RACK response size: %d\n", RARRAY_LEN(ret));
			return -1;
		}

		// manage Status

		status = rb_obj_as_string(RARRAY_PTR(ret)[0]);
		// get the status code

		wsgi_req->hvec[0].iov_base = wsgi_req->protocol;
        	wsgi_req->hvec[0].iov_len = wsgi_req->protocol_len ;

		wsgi_req->hvec[1].iov_base = (char *) " ";
        	wsgi_req->hvec[1].iov_len = 1 ;

        	wsgi_req->hvec[2].iov_base = RSTRING_PTR(status);
        	wsgi_req->hvec[2].iov_len = 3 ;

		wsgi_req->status = atoi(RSTRING_PTR(status));

        	wsgi_req->hvec[3].iov_base = (char *) " ";
        	wsgi_req->hvec[3].iov_len = 1 ;

        	wsgi_req->hvec[4].iov_len = 0 ;

        	for (http_sc = hsc; http_sc->message != NULL; http_sc++) {
                	if (!strncmp(http_sc->key, RSTRING_PTR(status), 3)) {
                        	wsgi_req->hvec[4].iov_base = (char *) http_sc->message ;
                        	wsgi_req->hvec[4].iov_len = http_sc->message_size ;
                        	break;
                	}
        	}

        	wsgi_req->hvec[5].iov_base = (char *) "\r\n";
        	wsgi_req->hvec[5].iov_len = 2 ;

		if ( !(wsgi_req->headers_size = wsgi_req->socket->proto_writev_header(wsgi_req, wsgi_req->hvec, 6)) ) {
                	uwsgi_error("writev()");
        	}

		headers = RARRAY_PTR(ret)[1] ;
		if (rb_respond_to( headers, rb_intern("each") )) {
			if (ur.unprotected) {
#ifdef RUBY19
        			rb_block_call(headers, rb_intern("each"), 0, 0, send_header, headers);
#else
        			rb_iterate(rb_each, headers, send_header, headers);
#endif
			}
			else {
				rb_protect( iterate_headers, headers, &error);
				if (error) {
					uwsgi_ruby_exception();
					rb_gc_unregister_address(&status);
                			rb_gc_unregister_address(&headers);
					goto clear;
				}
			}
		}

		if (wsgi_req->socket->proto_write(wsgi_req, "\r\n", 2) != 2) {
			uwsgi_error("write()");
		}

		body = RARRAY_PTR(ret)[2] ;

		if (rb_respond_to( body, rb_intern("to_path") )) {
			VALUE sendfile_path = rb_funcall( body, rb_intern("to_path"), 0);
			wsgi_req->sendfile_fd = open(RSTRING_PTR(sendfile_path), O_RDONLY);
			wsgi_req->response_size = uwsgi_sendfile(wsgi_req);
			if (wsgi_req->response_size > 0) {
				while(wsgi_req->response_size < wsgi_req->sendfile_fd_size) {
					//uwsgi_log("sendfile_fd_size = %d\n", wsgi_req->sendfile_fd_size);
					wsgi_req->response_size += uwsgi_sendfile(wsgi_req);
				}
			}
			rb_gc_unregister_address(&sendfile_path);

		}
		else if (rb_respond_to( body, rb_intern("each") )) {
			if (ur.unprotected) {
#ifdef RUBY19
        			rb_block_call(body, rb_intern("each"), 0, 0, send_body, 0);
#else
        			rb_iterate(rb_each, body, send_body, 0);
#endif
			}
			else {
				rb_protect( iterate_body, body, &error);
				if (error) {
					uwsgi_ruby_exception();
				}
			}
		}

		if (rb_respond_to( body, rb_intern("close") )) {
			//uwsgi_log("calling close\n");
			rb_funcall( body, rb_intern("close"), 0);
		}

//fine:

		/* unregister all the objects created */
	
		rb_gc_unregister_address(&status);
		rb_gc_unregister_address(&headers);
		rb_gc_unregister_address(&body);

	}
	else {
		internal_server_error(wsgi_req, "Invalid RACK response");
	}

clear:

	rb_gc_unregister_address(&ret);

	rb_gc_unregister_address(&env);

	if (ur.gc_freq <= 1 || ur.cycles%ur.gc_freq == 0) {
#ifdef UWSGI_DEBUG
			uwsgi_log("calling ruby GC\n");
#endif
			rb_gc();
	}

	ur.cycles++;



	return 0;
}

void uwsgi_rack_after_request(struct wsgi_request *wsgi_req) {

	if (uwsgi.shared->options[UWSGI_OPTION_LOGGING])
		log_request(wsgi_req);
}

int uwsgi_rack_manage_options(int i, char *optarg) {


	switch(i) {
		case LONG_ARGS_RAILS:
			// HACK: can be overridden with --post-buffering
			if (!uwsgi.post_buffering) uwsgi.post_buffering = 4096;
			ur.rails = optarg;
			return 1;
		case LONG_ARGS_RACK:
			// HACK: can be overridden with --post-buffering
			if (!uwsgi.post_buffering) uwsgi.post_buffering = 4096;
			ur.rack = optarg;
			return 1;
		case LONG_ARGS_RUBY_GC_FREQ:
			ur.gc_freq = atoi(optarg);
			return 1;
		case LONG_ARGS_RUBY_REQUIRE:
			uwsgi_string_new_list(&ur.rbrequire, optarg);
			return 1;
	}

	return 0;
}

void uwsgi_rack_suspend(struct wsgi_request *wsgi_req) {

	uwsgi_log("SUSPENDING RUBY\n");
}

void uwsgi_rack_resume(struct wsgi_request *wsgi_req) {

	uwsgi_log("RESUMING RUBY\n");
}

VALUE init_rack_app( VALUE script ) {

	int error;

#ifndef RUBY19
	rb_require("rubygems");
#endif
        rb_protect( require_rack, 0, &error ) ;
        if (error) {
        	uwsgi_ruby_exception();
		return Qnil;
        }

        VALUE rack = rb_const_get(rb_cObject, rb_intern("Rack"));
        VALUE rackup = rb_funcall( rb_const_get(rack, rb_intern("Builder")), rb_intern("parse_file"), 1, script);
        if (TYPE(rackup) != T_ARRAY) {
        	uwsgi_log("unable to parse %s file\n", RSTRING_PTR(script));
                return Qnil;
        }

        if (RARRAY_LEN(rackup) < 1) {
        	uwsgi_log("invalid rack config file: %s\n", RSTRING_PTR(script));
		return Qnil;
        }

        return RARRAY_PTR(rackup)[0] ;
}

int uwsgi_rack_xml(char *node, char *content) {

	int error;

	if (!strcmp("rack", node)) {
		ur.dispatcher = rb_protect(init_rack_app, rb_str_new2(content), &error);
		if (ur.dispatcher != Qnil) {
			rb_gc_register_address(&ur.dispatcher);
			uwsgi_log("Rack application ready\n");
			return 1;
		}
        }

	return 0;
}

int uwsgi_rack_magic(char *mountpoint, char *lazy) {

	if (!strcmp(lazy+strlen(lazy)-3, ".ru")) {
                ur.rack = lazy;
                return 1;
        }
        else if (!strcmp(lazy+strlen(lazy)-3, ".rb")) {
                ur.rack = lazy;
                return 1;
        }


	return 0;
}

/*
int uwsgi_rack_mount_app(char *mountpoint, char *app) {

	
        if ( !strcmp(what+strlen(what)-3, ".ru") || !strcmp(what+strlen(what)-3, ".rb")) {
                return = uwsgi_rack_load(mountpoint, what);
        }

        return -1;
}
*/

void uwsgi_rack_hijack(void) {
}

int uwsgi_rack_mule(char *opt) {
	int error = 0;

        if (uwsgi_endswith(opt, ".rb")) {
		rb_protect( uwsgi_require_file, rb_str_new2(opt), &error ) ;
                if (error) {
                        uwsgi_ruby_exception();
			return 0;
                }
                return 1;
        }

        return 0;

}

VALUE uwsgi_rb_pfh(VALUE args) {
	
	VALUE uwsgi_rb_embedded = rb_const_get(rb_cObject, rb_intern("UWSGI"));
	if (rb_respond_to(uwsgi_rb_embedded, rb_intern("post_fork_hook"))) {
		return rb_funcall(uwsgi_rb_embedded, rb_intern("post_fork_hook"), 0);
	}
	return Qnil;
}

void uwsgi_rb_post_fork() {
	int error = 0;

        // call the post_fork_hook
	rb_protect(uwsgi_rb_pfh, 0, &error);
	if (error) {
		uwsgi_ruby_exception();
	}
}

VALUE rack_call_signal_handler(VALUE args) {

        return rb_funcall(rb_ary_entry(args, 0), rb_intern("call"), 1, rb_ary_entry(args, 1));
}

int uwsgi_rack_signal_handler(uint8_t sig, void *handler) {

        int error = 0;


        VALUE rbhandler = (VALUE) handler;
        VALUE args = rb_ary_new2(2);
        rb_ary_store(args, 0, rbhandler);
        VALUE rbsig = INT2NUM(sig);
        rb_ary_store(args, 1, rbsig);
        VALUE ret = rb_protect(rack_call_signal_handler, args, &error);
        if (error) {
                uwsgi_ruby_exception();
                // free resources (useless ?)
                rb_gc_unregister_address(&args);
                rb_gc_unregister_address(&ret);
                rb_gc_unregister_address(&rbsig);
                rb_gc();
                return -1;
        }

        // free resources (useless ?)
        rb_gc_unregister_address(&args);
        rb_gc_unregister_address(&ret);
        rb_gc_unregister_address(&rbsig);
        rb_gc();

        return 0;
}




struct uwsgi_plugin rack_plugin = {

	.name = "rack",
	.modifier1 = 7,
	.init = uwsgi_rack_init,
	.options = uwsgi_rack_options,
	.manage_opt = uwsgi_rack_manage_options,
	.request = uwsgi_rack_request,
	.after_request = uwsgi_rack_after_request,

	.signal_handler = uwsgi_rack_signal_handler,

	.hijack_worker = uwsgi_rack_hijack,
	.post_fork = uwsgi_rb_post_fork,

	.init_apps = uwsgi_rack_init_apps,
	//.mount_app = uwsgi_rack_mount_app,
	.manage_xml = uwsgi_rack_xml,

	.magic = uwsgi_rack_magic,

	.mule = uwsgi_rack_mule,
	.rpc = uwsgi_ruby_rpc,

	.suspend = uwsgi_rack_suspend,
	.resume = uwsgi_rack_resume,
};

