#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <iostream>
#include <fstream>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <curl/curl.h>

#include "daemon.h"
#include "smacros.h"
#include "ServiceContext.h"

// ---- gsoap ----
#include "DeviceBinding.nsmap"
#include "soapDeviceBindingService.h"
#include "soapMediaBindingService.h"
#include "soapPTZBindingService.h"

static const char *help_str =
    " ===============  Help  ===============\n"
    " Daemon name:  " DAEMON_NAME "\n"
    " Daemon  ver:  " DAEMON_VERSION_STR "\n"
#ifdef DEBUG
    " Build  mode:  debug\n"
#else
    " Build  mode:  release\n"
#endif
    " Build  date:  " __DATE__ "\n"
    " Build  time:  " __TIME__ "\n\n"
    "Options:                      description:\n\n"
    "       --no_chdir             Don't change the directory to '/'\n"
    "       --no_fork              Don't do fork\n"
    "       --no_close             Don't close standart IO files\n"
    "       --conf_file          [value] Set configuration file name\n"
    "       --pid_file           [value] Set pid file name\n"
    "       --log_file           [value] Set log file name\n\n"
    "       --port               [value] Set socket port for Services   (default = 1000)\n"
    "       --user               [value] Set user name for Services     (default = admin)\n"
    "       --password           [value] Set user password for Services (default = admin)\n"
    "       --model              [value] Set model device for Services  (default = Model)\n"
    "       --scope              [value] Set scope for Services         (default don't set)\n"
    "       --ifs                [value] Set Net interfaces for work    (default don't set)\n"
    "       --hardware_id        [value] Set Hardware ID of device      (default = HardwareID)\n"
    "       --serial_num         [value] Set Serial number of device    (default = SerialNumber)\n"
    "       --firmware_ver       [value] Set firmware version of device (default = FirmwareVersion)\n"
    "       --manufacturer       [value] Set manufacturer for Services  (default = Manufacturer)\n\n"
    "       --name               [value] Set Name for Profile Media Services\n"
    "       --width              [value] Set Width for Profile Media Services\n"
    "       --height             [value] Set Height for Profile Media Services\n"
    "       --url                [value] Set URL (or template URL) for Profile Media Services\n"
    "       --snapurl            [value] Set URL (or template URL) for Snapshot\n"
    "                                    in template mode %s will be changed to IP of interface (see opt ifs)\n"
    "       --type               [value] Set Type for Profile Media Services (JPEG|MPEG4|H264)\n"
    "                                    It is also a sign of the end of the profile parameters\n\n"
    "       --ptz                        Enable PTZ support\n"
    "       --move_continuous    [value] Set process to call for PTZ continuous movement\n"
    "       --move_stop          [value] Set process to call for PTZ stop movement\n"
    "       --move_preset        [value] Set process to call for PTZ goto preset movement\n"
    "  -v,  --version              Display daemon version\n"
    "  -h,  --help                 Display this help\n\n";

// indexes for long_opt function
namespace LongOpts
{
    enum
    {
        version = 'v',
        help = 'h',

        //daemon options
        no_chdir = 1,
        no_fork,
        no_close,
        conf_file,
        pid_file,
        log_file,

        //ONVIF Service options (context)
        port,
        user,
        password,
        manufacturer,
        model,
        firmware_ver,
        serial_num,
        hardware_id,
        scope,
        ifs,

        //Media Profile for ONVIF Media Service
        name,
        width,
        height,
        url,
        snapurl,
        type,

        //PTZ Profile for ONVIF PTZ Service
        ptz,
        move_continuous,
        move_stop,
        goto_preset,
        goto_home
    };
}

static const char *short_opts = "hv";

static const struct option long_opts[] =
    {
        {"version", no_argument, NULL, LongOpts::version},
        {"help", no_argument, NULL, LongOpts::help},

        //daemon options
        {"no_chdir", no_argument, NULL, LongOpts::no_chdir},
        {"no_fork", no_argument, NULL, LongOpts::no_fork},
        {"no_close", no_argument, NULL, LongOpts::no_close},
        {"conf_file", required_argument, NULL, LongOpts::conf_file},
        {"pid_file", required_argument, NULL, LongOpts::pid_file},
        {"log_file", required_argument, NULL, LongOpts::log_file},

        //ONVIF Service options (context)
        {"port", required_argument, NULL, LongOpts::port},
        {"user", required_argument, NULL, LongOpts::user},
        {"password", required_argument, NULL, LongOpts::password},
        {"manufacturer", required_argument, NULL, LongOpts::manufacturer},
        {"model", required_argument, NULL, LongOpts::model},
        {"firmware_ver", required_argument, NULL, LongOpts::firmware_ver},
        {"serial_num", required_argument, NULL, LongOpts::serial_num},
        {"hardware_id", required_argument, NULL, LongOpts::hardware_id},
        {"scope", required_argument, NULL, LongOpts::scope},
        {"ifs", required_argument, NULL, LongOpts::ifs},

        //Media Profile for ONVIF Media Service
        {"name", required_argument, NULL, LongOpts::name},
        {"width", required_argument, NULL, LongOpts::width},
        {"height", required_argument, NULL, LongOpts::height},
        {"url", required_argument, NULL, LongOpts::url},
        {"snapurl", required_argument, NULL, LongOpts::snapurl},
        {"type", required_argument, NULL, LongOpts::type},

        //PTZ Profile for ONVIF PTZ Service
        {"ptz", no_argument, NULL, LongOpts::ptz},
        {"move_continuous", required_argument, NULL, LongOpts::move_continuous},
        {"move_stop", required_argument, NULL, LongOpts::move_stop},
        {"goto_preset", required_argument, NULL, LongOpts::goto_preset},
        {"goto_home", required_argument, NULL, LongOpts::goto_home},

        {NULL, no_argument, NULL, 0}};

#define FOREACH_SERVICE(APPLY, soap)  \
    APPLY(DeviceBindingService, soap) \
    APPLY(MediaBindingService, soap)  \
    APPLY(PTZBindingService, soap)

/*
 * If you need support for other services,
 * add the desired option to the macro FOREACH_SERVICE.
 *
 * Note: Do not forget to add the gsoap binding class for the service,
 * and the implementation methods for it, like for DeviceBindingService



        APPLY(ImagingBindingService, soap)               \
        APPLY(PTZBindingService, soap)                   \
        APPLY(RecordingBindingService, soap)             \
        APPLY(ReplayBindingService, soap)                \
        APPLY(SearchBindingService, soap)                \
        APPLY(ReceiverBindingService, soap)              \
        APPLY(DisplayBindingService, soap)               \
        APPLY(EventBindingService, soap)                 \
        APPLY(PullPointSubscriptionBindingService, soap) \
        APPLY(NotificationProducerBindingService, soap)  \
        APPLY(SubscriptionManagerBindingService, soap)   \
*/

#define DECLARE_SERVICE(service, soap) service service##_inst(soap);

#define DISPATCH_SERVICE(service, soap)                   \
    else if (service##_inst.dispatch() != SOAP_NO_METHOD) \
    {                                                     \
        soap_send_fault(soap);                            \
        soap_stream_fault(soap, std::cerr);               \
    }

static struct soap *soap;

ServiceContext service_ctx;

void daemon_exit_handler(int sig)
{
    //Here we release resources

    UNUSED(sig);
    soap_destroy(soap); // delete managed C++ objects
    soap_end(soap);     // delete managed memory
    soap_free(soap);    // free the context

    unlink(daemon_info.pid_file);

    curl_global_cleanup();

    exit(EXIT_SUCCESS); // good job (we interrupted (finished) main loop)
}

void init_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = daemon_exit_handler;
    if (sigaction(SIGTERM, &sa, NULL) != 0)
        daemon_error_exit("Can't set daemon_exit_handler: %m\n");

    signal(SIGCHLD, SIG_IGN); // ignore child
    signal(SIGTSTP, SIG_IGN); // ignore tty signals
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
}

void processing_cmd(int argc, char *argv[])
{
    int opt;

    StreamProfile profile;

    while ((opt = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1)
    {
        switch (opt)
        {

        case LongOpts::help:
            puts(help_str);
            exit_if_not_daemonized(EXIT_SUCCESS);
            break;

        case LongOpts::version:
            puts(DAEMON_NAME "  version  " DAEMON_VERSION_STR "\n");
            exit_if_not_daemonized(EXIT_SUCCESS);
            break;

        //daemon options
        case LongOpts::no_chdir:
            daemon_info.no_chdir = 1;
            break;

        case LongOpts::no_fork:
            daemon_info.no_fork = 1;
            break;

        case LongOpts::no_close:
            daemon_info.no_close_stdio = 1;
            break;

        case LongOpts::conf_file:
            daemon_info.conf_file = optarg;
            break;

        case LongOpts::pid_file:
            daemon_info.pid_file = optarg;
            break;

        case LongOpts::log_file:
            daemon_info.log_file = optarg;
            break;

        //ONVIF Service options (context)
        case LongOpts::port:
            service_ctx.port = atoi(optarg);
            break;

        case LongOpts::user:
            service_ctx.user = optarg;
            break;

        case LongOpts::password:
            service_ctx.password = optarg;
            break;

        case LongOpts::manufacturer:
            service_ctx.manufacturer = optarg;
            break;

        case LongOpts::model:
            service_ctx.model = optarg;
            break;

        case LongOpts::firmware_ver:
            service_ctx.firmware_version = optarg;
            break;

        case LongOpts::serial_num:
            service_ctx.serial_number = optarg;
            break;

        case LongOpts::hardware_id:
            service_ctx.hardware_id = optarg;
            break;

        case LongOpts::scope:
            service_ctx.scopes.push_back(optarg);
            break;

        case LongOpts::ifs:
            service_ctx.eth_ifs.push_back(Eth_Dev_Param());

            if (service_ctx.eth_ifs.back().open(optarg) != 0)
                daemon_error_exit("Can't open ethernet interface: %s - %m\n", optarg);

            break;

        //Media Profile for ONVIF Media Service
        case LongOpts::name:
            if (!profile.set_name(optarg))
                daemon_error_exit("Can't set name for Profile: %s\n", profile.get_cstr_err());

            break;

        case LongOpts::width:
            if (!profile.set_width(optarg))
                daemon_error_exit("Can't set width for Profile: %s\n", profile.get_cstr_err());

            break;

        case LongOpts::height:
            if (!profile.set_height(optarg))
                daemon_error_exit("Can't set height for Profile: %s\n", profile.get_cstr_err());

            break;

        case LongOpts::url:
            if (!profile.set_url(optarg))
                daemon_error_exit("Can't set URL for Profile: %s\n", profile.get_cstr_err());

            break;

        case LongOpts::snapurl:
            if (!profile.set_snapurl(optarg))
                daemon_error_exit("Can't set URL for Snapshot: %s\n", profile.get_cstr_err());

            break;

        case LongOpts::type:
            if (!profile.set_type(optarg))
                daemon_error_exit("Can't set type for Profile: %s\n", profile.get_cstr_err());

            if (!service_ctx.add_profile(profile))
                daemon_error_exit("Can't add Profile: %s\n", service_ctx.get_cstr_err());

            profile.clear(); //now we can add new profile (just uses one variable)

            break;

        //PTZ Profile for ONVIF PTZ Service
        case LongOpts::ptz:
            service_ctx.get_ptz_node()->enable = true;
            break;

        case LongOpts::move_continuous:
            if (!service_ctx.get_ptz_node()->set_move_continuous(optarg))
                daemon_error_exit("Can't set url for continuous movement: %s\n", service_ctx.get_ptz_node()->get_cstr_err());
            break;

        case LongOpts::move_stop:
            if (!service_ctx.get_ptz_node()->set_move_stop(optarg))
                daemon_error_exit("Can't set url for stop movement: %s\n", service_ctx.get_ptz_node()->get_cstr_err());

            break;

        case LongOpts::goto_preset:
            if (!service_ctx.get_ptz_node()->set_goto_preset(optarg))
                daemon_error_exit("Can't set url for goto preset movement: %s\n", service_ctx.get_ptz_node()->get_cstr_err());

            break;
        case LongOpts::goto_home:
            if (!service_ctx.get_ptz_node()->set_goto_home(optarg))
                daemon_error_exit("Can't set url for goto home movement: %s\n", service_ctx.get_ptz_node()->get_cstr_err());

            break;

        default:
            puts("for more detail see help\n\n");
            exit_if_not_daemonized(EXIT_FAILURE);
            break;
        }
    }
}

void processing_conf_file()
{
    StreamProfile profile;

    std::string line;
    std::string param;
    std::string value;
    std::ifstream filein(daemon_info.conf_file);

    while (std::getline(filein, line))
    {
        unsigned int first = 0;
        unsigned int second;

        if (line.size() == 0)
        {
            continue;
        }
        if (line.at(0) == '#')
        {
            continue;
        }
        second = line.find_first_of('=', first);
        //first has index of start of token
        //second has index of end of token + 1;
        if (second == std::string::npos)
        {
            second = line.size();
        }
        param = line.substr(first, second - first);
        if (second == line.size())
        {
            daemon_error_exit("Wrong option: %s\n", line.c_str());
        }
        else if (second == line.size() - 1)
        {
            value = "";
        }
        else
        {
            value = line.substr(second + 1, line.size() - second);
        }
        if ((value != "") && (value.at(0) == '"') && (value.at(value.size() - 1) == '"'))
        {
            value = value.substr(1, value.size() - 2);
        }
        //        fprintf(stderr, "%s: %s\n", param.c_str(), value.c_str());

        //daemon options
        if (param == "no_chdir")
        {
            if (value == "1")
            {
                daemon_info.no_chdir = 1;
            }
        }
        else if (param == "no_fork")
        {
            if (value == "1")
            {
                daemon_info.no_fork = 1;
            }
        }
        else if (param == "no_close")
        {
            if (value == "1")
            {
                daemon_info.no_close_stdio = 1;
            }
        }
        else if (param == "pid_file")
        {
            daemon_info.pid_file = (char *)malloc(value.size() + 1);
            strcpy(daemon_info.pid_file, value.c_str());
        }
        else if (param == "log_file")
        {
            daemon_info.log_file = (char *)malloc(value.size() + 1);
            strcpy(daemon_info.log_file, value.c_str());

            //ONVIF Service options (context)
        }
        else if (param == "port")
        {
            service_ctx.port = std::stoi(value);
        }
        else if (param == "user")
        {
            service_ctx.user = value;
        }
        else if (param == "password")
        {
            service_ctx.password = value;
        }
        else if (param == "manufacturer")
        {
            service_ctx.manufacturer = value;
        }
        else if (param == "model")
        {
            service_ctx.model = value;
        }
        else if (param == "firmware_ver")
        {
            service_ctx.firmware_version = value;
        }
        else if (param == "serial_num")
        {
            service_ctx.serial_number = value;
        }
        else if (param == "hardware_id")
        {
            service_ctx.hardware_id = value;
        }
        else if (param == "scope")
        {
            service_ctx.scopes.push_back(value);
        }
        else if (param == "ifs")
        {
            service_ctx.eth_ifs.push_back(Eth_Dev_Param());
            if (service_ctx.eth_ifs.back().open(value.c_str()) != 0)
                daemon_error_exit("Can't open ethernet interface: %s - %m\n", value.c_str());

            //Media Profile for ONVIF Media Service
        }
        else if (param == "name")
        {
            if (!profile.set_name(value.c_str()))
                daemon_error_exit("Can't set name for Profile: %s\n", profile.get_cstr_err());
        }
        else if (param == "width")
        {
            if (!profile.set_width(value.c_str()))
                daemon_error_exit("Can't set width for Profile: %s\n", profile.get_cstr_err());
        }
        else if (param == "height")
        {
            if (!profile.set_height(value.c_str()))
                daemon_error_exit("Can't set height for Profile: %s\n", profile.get_cstr_err());
        }
        else if (param == "url")
        {
            if (!profile.set_url(value.c_str()))
                daemon_error_exit("Can't set URL for Profile: %s\n", profile.get_cstr_err());
        }
        else if (param == "snapurl")
        {
            if (!profile.set_snapurl(value.c_str()))
                daemon_error_exit("Can't set URL for Snapshot: %s\n", profile.get_cstr_err());
        }
        else if (param == "type")
        {
            if (!profile.set_type(value.c_str()))
                daemon_error_exit("Can't set type for Profile: %s\n", profile.get_cstr_err());

            if (!service_ctx.add_profile(profile))
                daemon_error_exit("Can't add Profile: %s\n", service_ctx.get_cstr_err());

            profile.clear(); //now we can add new profile (just uses one variable)

            //PTZ Profile for ONVIF PTZ Service
        }
        else if (param == "ptz")
        {
            service_ctx.get_ptz_node()->enable = true;
        }
        else if (param == "move_continuous")
        {
            if (!service_ctx.get_ptz_node()->set_move_continuous(value.c_str()))
                daemon_error_exit("Can't set url for continuous movement: %s\n", service_ctx.get_ptz_node()->get_cstr_err());
        }
        else if (param == "move_stop")
        {
            if (!service_ctx.get_ptz_node()->set_move_stop(value.c_str()))
                daemon_error_exit("Can't set url for stop movement: %s\n", service_ctx.get_ptz_node()->get_cstr_err());
        }
        else if (param == "goto_preset")
        {
            if (!service_ctx.get_ptz_node()->set_goto_preset(value.c_str()))
                daemon_error_exit("Can't set url for goto preset movement: %s\n", service_ctx.get_ptz_node()->get_cstr_err());
        }
        else if (param == "goto_home")
        {
            if (!service_ctx.get_ptz_node()->set_goto_home(value.c_str()))
                daemon_error_exit("Can't set url for goto home movement: %s\n", service_ctx.get_ptz_node()->get_cstr_err());
        }
        else
        {
            daemon_error_exit("Unrecognized option: %s\n", line.c_str());
        }
    }
}

void check_service_ctx(void)
{
    if (service_ctx.eth_ifs.empty())
        daemon_error_exit("Error: not set no one ehternet interface more details see opt --ifs\n");

    if (service_ctx.scopes.empty())
        daemon_error_exit("Error: not set scopes more details see opt --scope\n");

    if (service_ctx.get_profiles().empty())
        daemon_error_exit("Error: not set no one profile more details see --help\n");
}

void init_gsoap(void)
{
    soap = soap_new();

    if (!soap)
        daemon_error_exit("Can't get mem for SOAP\n");

    soap->bind_flags = SO_REUSEADDR;

    if (!soap_valid_socket(soap_bind(soap, NULL, service_ctx.port, 10)))
    {
        soap_stream_fault(soap, std::cerr);
        exit(EXIT_FAILURE);
    }

    soap->send_timeout = 3; // timeout in sec
    soap->recv_timeout = 3; // timeout in sec

    //save pointer of service_ctx in soap
    soap->user = (void *)&service_ctx;
}

void init(void *data)
{
    UNUSED(data);
    init_signals();
    check_service_ctx();
    init_gsoap();
    curl_global_init(CURL_GLOBAL_ALL);
}

int main(int argc, char *argv[])
{
    processing_cmd(argc, argv);
    if (daemon_info.conf_file)
        processing_conf_file();
    daemonize2(init, NULL);

    FOREACH_SERVICE(DECLARE_SERVICE, soap)

    while (true)
    {
        // wait new client
        if (!soap_valid_socket(soap_accept(soap)))
        {
            soap_stream_fault(soap, std::cerr);
            return EXIT_FAILURE;
        }

        // process service
        if (soap_begin_serve(soap))
        {
            soap_stream_fault(soap, std::cerr);
        }
        FOREACH_SERVICE(DISPATCH_SERVICE, soap)
        else
        {
            DEBUG_MSG("Unknown service\n");
        }

        soap_destroy(soap); // delete managed C++ objects
        soap_end(soap);     // delete managed memory
    }

    return EXIT_FAILURE; // Error, normal exit from the main loop only through the signal handler.
}
