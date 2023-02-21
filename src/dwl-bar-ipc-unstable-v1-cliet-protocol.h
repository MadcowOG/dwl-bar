/* Generated by wayland-scanner 1.21.0 */

#ifndef DWL_BAR_IPC_UNSTABLE_V1_CLIENT_PROTOCOL_H
#define DWL_BAR_IPC_UNSTABLE_V1_CLIENT_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include "wayland-client.h"

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * @page page_dwl_bar_ipc_unstable_v1 The dwl_bar_ipc_unstable_v1 protocol
 * inter-proccess-communication about dwl's state
 *
 * @section page_desc_dwl_bar_ipc_unstable_v1 Description
 *
 * This protocol allows clients to get updates from dwl and vice versa.
 *
 * Warning! This protocol is experimental and may make backward incompatible changes.
 *
 * @section page_ifaces_dwl_bar_ipc_unstable_v1 Interfaces
 * - @subpage page_iface_zdwl_manager_v1 - manage dwl state
 * - @subpage page_iface_zdwl_output_v1 - control dwl output
 */
struct wl_output;
struct zdwl_manager_v1;
struct zdwl_output_v1;

#ifndef ZDWL_MANAGER_V1_INTERFACE
#define ZDWL_MANAGER_V1_INTERFACE
/**
 * @page page_iface_zdwl_manager_v1 zdwl_manager_v1
 * @section page_iface_zdwl_manager_v1_desc Description
 *
 * This interface is exposed as a global in wl_registry.
 *
 * Clients can use this interface to get a dwl_output.
 * After binding the client will revieve dwl_manager.tag and dwl_manager.layout events.
 * The dwl_manager.tag and dwl_manager.layout events expose tags and layouts to the client.
 * @section page_iface_zdwl_manager_v1_api API
 * See @ref iface_zdwl_manager_v1.
 */
/**
 * @defgroup iface_zdwl_manager_v1 The zdwl_manager_v1 interface
 *
 * This interface is exposed as a global in wl_registry.
 *
 * Clients can use this interface to get a dwl_output.
 * After binding the client will revieve dwl_manager.tag and dwl_manager.layout events.
 * The dwl_manager.tag and dwl_manager.layout events expose tags and layouts to the client.
 */
extern const struct wl_interface zdwl_manager_v1_interface;
#endif
#ifndef ZDWL_OUTPUT_V1_INTERFACE
#define ZDWL_OUTPUT_V1_INTERFACE
/**
 * @page page_iface_zdwl_output_v1 zdwl_output_v1
 * @section page_iface_zdwl_output_v1_desc Description
 *
 * Observe and control a dwl output.
 *
 * Events are double-buffered:
 * Clients should cache events and redraw when a dwl_output.done event is sent.
 *
 * Request are not double-buffered:
 * The compositor will update immediately upon request.
 * @section page_iface_zdwl_output_v1_api API
 * See @ref iface_zdwl_output_v1.
 */
/**
 * @defgroup iface_zdwl_output_v1 The zdwl_output_v1 interface
 *
 * Observe and control a dwl output.
 *
 * Events are double-buffered:
 * Clients should cache events and redraw when a dwl_output.done event is sent.
 *
 * Request are not double-buffered:
 * The compositor will update immediately upon request.
 */
extern const struct wl_interface zdwl_output_v1_interface;
#endif

/**
 * @ingroup iface_zdwl_manager_v1
 * @struct zdwl_manager_v1_listener
 */
struct zdwl_manager_v1_listener {
	/**
	 * Announces a tag
	 *
	 * This event is sent after binding. A roundtrip after binding
	 * guarantees the client recieved all tags.
	 */
	void (*tag)(void *data,
		    struct zdwl_manager_v1 *zdwl_manager_v1,
		    const char *name);
	/**
	 * Announces a layout
	 *
	 * This event is sent after binding. A roundtrip after binding
	 * guarantees the client recieved all layouts.
	 */
	void (*layout)(void *data,
		       struct zdwl_manager_v1 *zdwl_manager_v1,
		       const char *name);
};

/**
 * @ingroup iface_zdwl_manager_v1
 */
static inline int
zdwl_manager_v1_add_listener(struct zdwl_manager_v1 *zdwl_manager_v1,
			     const struct zdwl_manager_v1_listener *listener, void *data)
{
	return wl_proxy_add_listener((struct wl_proxy *) zdwl_manager_v1,
				     (void (**)(void)) listener, data);
}

#define ZDWL_MANAGER_V1_RELEASE 0
#define ZDWL_MANAGER_V1_GET_OUTPUT 1

/**
 * @ingroup iface_zdwl_manager_v1
 */
#define ZDWL_MANAGER_V1_TAG_SINCE_VERSION 1
/**
 * @ingroup iface_zdwl_manager_v1
 */
#define ZDWL_MANAGER_V1_LAYOUT_SINCE_VERSION 1

/**
 * @ingroup iface_zdwl_manager_v1
 */
#define ZDWL_MANAGER_V1_RELEASE_SINCE_VERSION 1
/**
 * @ingroup iface_zdwl_manager_v1
 */
#define ZDWL_MANAGER_V1_GET_OUTPUT_SINCE_VERSION 1

/** @ingroup iface_zdwl_manager_v1 */
static inline void
zdwl_manager_v1_set_user_data(struct zdwl_manager_v1 *zdwl_manager_v1, void *user_data)
{
	wl_proxy_set_user_data((struct wl_proxy *) zdwl_manager_v1, user_data);
}

/** @ingroup iface_zdwl_manager_v1 */
static inline void *
zdwl_manager_v1_get_user_data(struct zdwl_manager_v1 *zdwl_manager_v1)
{
	return wl_proxy_get_user_data((struct wl_proxy *) zdwl_manager_v1);
}

static inline uint32_t
zdwl_manager_v1_get_version(struct zdwl_manager_v1 *zdwl_manager_v1)
{
	return wl_proxy_get_version((struct wl_proxy *) zdwl_manager_v1);
}

/** @ingroup iface_zdwl_manager_v1 */
static inline void
zdwl_manager_v1_destroy(struct zdwl_manager_v1 *zdwl_manager_v1)
{
	wl_proxy_destroy((struct wl_proxy *) zdwl_manager_v1);
}

/**
 * @ingroup iface_zdwl_manager_v1
 *
 * Indicates that the client will not the dwl_manager object anymore.
 * Objects created through this instance are not affected.
 */
static inline void
zdwl_manager_v1_release(struct zdwl_manager_v1 *zdwl_manager_v1)
{
	wl_proxy_marshal_flags((struct wl_proxy *) zdwl_manager_v1,
			 ZDWL_MANAGER_V1_RELEASE, NULL, wl_proxy_get_version((struct wl_proxy *) zdwl_manager_v1), WL_MARSHAL_FLAG_DESTROY);
}

/**
 * @ingroup iface_zdwl_manager_v1
 *
 * Get a dwl_output for the specified wl_output.
 */
static inline struct zdwl_output_v1 *
zdwl_manager_v1_get_output(struct zdwl_manager_v1 *zdwl_manager_v1, struct wl_output *output)
{
	struct wl_proxy *id;

	id = wl_proxy_marshal_flags((struct wl_proxy *) zdwl_manager_v1,
			 ZDWL_MANAGER_V1_GET_OUTPUT, &zdwl_output_v1_interface, wl_proxy_get_version((struct wl_proxy *) zdwl_manager_v1), 0, NULL, output);

	return (struct zdwl_output_v1 *) id;
}

#ifndef ZDWL_OUTPUT_V1_TAG_STATE_ENUM
#define ZDWL_OUTPUT_V1_TAG_STATE_ENUM
enum zdwl_output_v1_tag_state {
	/**
	 * no state
	 */
	ZDWL_OUTPUT_V1_TAG_STATE_NONE = 0,
	/**
	 * tag is active
	 */
	ZDWL_OUTPUT_V1_TAG_STATE_ACTIVE = 1,
	/**
	 * tag has at least one urgent client
	 */
	ZDWL_OUTPUT_V1_TAG_STATE_URGENT = 2,
};
#endif /* ZDWL_OUTPUT_V1_TAG_STATE_ENUM */

/**
 * @ingroup iface_zdwl_output_v1
 * @struct zdwl_output_v1_listener
 */
struct zdwl_output_v1_listener {
	/**
	 * Toggle client visibilty
	 *
	 * Indicates the client should hide or show themselves. If the
	 * client is visible then hide, if hidden then show.
	 */
	void (*toggle_visibility)(void *data,
				  struct zdwl_output_v1 *zdwl_output_v1);
	/**
	 * Update the selected output.
	 *
	 * Indicates if the output is active. Zero is invalid, nonzero is
	 * valid.
	 */
	void (*active)(void *data,
		       struct zdwl_output_v1 *zdwl_output_v1,
		       uint32_t active);
	/**
	 * Update the state of a tag.
	 *
	 * Indicates that a tag has been updated.
	 * @param tag Index of the tag
	 * @param state The state of the tag.
	 * @param clients The number of clients in the tag.
	 * @param focused If there is a focused client. Nonzero being valid, zero being invalid.
	 */
	void (*tag)(void *data,
		    struct zdwl_output_v1 *zdwl_output_v1,
		    uint32_t tag,
		    uint32_t state,
		    uint32_t clients,
		    uint32_t focused);
	/**
	 * Update the layout.
	 *
	 * Indicates a new layout is selected.
	 * @param layout Index of the layout.
	 */
	void (*layout)(void *data,
		       struct zdwl_output_v1 *zdwl_output_v1,
		       uint32_t layout);
	/**
	 * Update the title.
	 *
	 * Indicates the title has changed.
	 * @param title The new title name.
	 */
	void (*title)(void *data,
		      struct zdwl_output_v1 *zdwl_output_v1,
		      const char *title);
	/**
	 * The update sequence is done.
	 *
	 * Indicates that a sequence of status updates have finished and
	 * the client should redraw.
	 */
	void (*frame)(void *data,
		      struct zdwl_output_v1 *zdwl_output_v1);
};

/**
 * @ingroup iface_zdwl_output_v1
 */
static inline int
zdwl_output_v1_add_listener(struct zdwl_output_v1 *zdwl_output_v1,
			    const struct zdwl_output_v1_listener *listener, void *data)
{
	return wl_proxy_add_listener((struct wl_proxy *) zdwl_output_v1,
				     (void (**)(void)) listener, data);
}

#define ZDWL_OUTPUT_V1_RELEASE 0
#define ZDWL_OUTPUT_V1_SET_LAYOUT 1
#define ZDWL_OUTPUT_V1_SET_TAGS 2
#define ZDWL_OUTPUT_V1_SET_CLIENT_TAGS 3

/**
 * @ingroup iface_zdwl_output_v1
 */
#define ZDWL_OUTPUT_V1_TOGGLE_VISIBILITY_SINCE_VERSION 1
/**
 * @ingroup iface_zdwl_output_v1
 */
#define ZDWL_OUTPUT_V1_ACTIVE_SINCE_VERSION 1
/**
 * @ingroup iface_zdwl_output_v1
 */
#define ZDWL_OUTPUT_V1_TAG_SINCE_VERSION 1
/**
 * @ingroup iface_zdwl_output_v1
 */
#define ZDWL_OUTPUT_V1_LAYOUT_SINCE_VERSION 1
/**
 * @ingroup iface_zdwl_output_v1
 */
#define ZDWL_OUTPUT_V1_TITLE_SINCE_VERSION 1
/**
 * @ingroup iface_zdwl_output_v1
 */
#define ZDWL_OUTPUT_V1_FRAME_SINCE_VERSION 1

/**
 * @ingroup iface_zdwl_output_v1
 */
#define ZDWL_OUTPUT_V1_RELEASE_SINCE_VERSION 1
/**
 * @ingroup iface_zdwl_output_v1
 */
#define ZDWL_OUTPUT_V1_SET_LAYOUT_SINCE_VERSION 1
/**
 * @ingroup iface_zdwl_output_v1
 */
#define ZDWL_OUTPUT_V1_SET_TAGS_SINCE_VERSION 1
/**
 * @ingroup iface_zdwl_output_v1
 */
#define ZDWL_OUTPUT_V1_SET_CLIENT_TAGS_SINCE_VERSION 1

/** @ingroup iface_zdwl_output_v1 */
static inline void
zdwl_output_v1_set_user_data(struct zdwl_output_v1 *zdwl_output_v1, void *user_data)
{
	wl_proxy_set_user_data((struct wl_proxy *) zdwl_output_v1, user_data);
}

/** @ingroup iface_zdwl_output_v1 */
static inline void *
zdwl_output_v1_get_user_data(struct zdwl_output_v1 *zdwl_output_v1)
{
	return wl_proxy_get_user_data((struct wl_proxy *) zdwl_output_v1);
}

static inline uint32_t
zdwl_output_v1_get_version(struct zdwl_output_v1 *zdwl_output_v1)
{
	return wl_proxy_get_version((struct wl_proxy *) zdwl_output_v1);
}

/** @ingroup iface_zdwl_output_v1 */
static inline void
zdwl_output_v1_destroy(struct zdwl_output_v1 *zdwl_output_v1)
{
	wl_proxy_destroy((struct wl_proxy *) zdwl_output_v1);
}

/**
 * @ingroup iface_zdwl_output_v1
 *
 * Indicates to that the client no longer needs this dwl_output.
 */
static inline void
zdwl_output_v1_release(struct zdwl_output_v1 *zdwl_output_v1)
{
	wl_proxy_marshal_flags((struct wl_proxy *) zdwl_output_v1,
			 ZDWL_OUTPUT_V1_RELEASE, NULL, wl_proxy_get_version((struct wl_proxy *) zdwl_output_v1), WL_MARSHAL_FLAG_DESTROY);
}

/**
 * @ingroup iface_zdwl_output_v1
 */
static inline void
zdwl_output_v1_set_layout(struct zdwl_output_v1 *zdwl_output_v1, uint32_t index)
{
	wl_proxy_marshal_flags((struct wl_proxy *) zdwl_output_v1,
			 ZDWL_OUTPUT_V1_SET_LAYOUT, NULL, wl_proxy_get_version((struct wl_proxy *) zdwl_output_v1), 0, index);
}

/**
 * @ingroup iface_zdwl_output_v1
 */
static inline void
zdwl_output_v1_set_tags(struct zdwl_output_v1 *zdwl_output_v1, uint32_t tagmask, uint32_t toggle_tagset)
{
	wl_proxy_marshal_flags((struct wl_proxy *) zdwl_output_v1,
			 ZDWL_OUTPUT_V1_SET_TAGS, NULL, wl_proxy_get_version((struct wl_proxy *) zdwl_output_v1), 0, tagmask, toggle_tagset);
}

/**
 * @ingroup iface_zdwl_output_v1
 *
 * The tags are updated as follows:
 * new_tags = (current_tags AND and_tags) XOR xor_tags
 */
static inline void
zdwl_output_v1_set_client_tags(struct zdwl_output_v1 *zdwl_output_v1, uint32_t and_tags, uint32_t xor_tags)
{
	wl_proxy_marshal_flags((struct wl_proxy *) zdwl_output_v1,
			 ZDWL_OUTPUT_V1_SET_CLIENT_TAGS, NULL, wl_proxy_get_version((struct wl_proxy *) zdwl_output_v1), 0, and_tags, xor_tags);
}

#ifdef  __cplusplus
}
#endif

#endif