/*******************************************************************************
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "jsonconverter.h"
#include "gva_utils.h"
#include "video_frame.h"
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

GST_DEBUG_CATEGORY_STATIC(gst_json_converter_debug);
#define GST_CAT_DEFAULT gst_json_converter_debug

json get_frame_data(GstGvaMetaConvert *converter, GstBuffer *buffer) {
    json res;
    GstSegment converter_segment = converter->base_gvametaconvert.segment;
    GstClockTime timestamp = gst_segment_to_stream_time(&converter_segment, GST_FORMAT_TIME, buffer->pts);
    if (converter->info)
        res["resolution"] = json::object({{"width", converter->info->width}, {"height", converter->info->height}});
    if (converter->source)
        res["source"] = converter->source;
    if (timestamp != G_MAXUINT64)
        res["timestamp"] = timestamp - converter_segment.time;
    if (converter->tags && json::accept(converter->tags))
        res["tags"] = json::parse(converter->tags);
    return res;
}

json convert_tensor(const GVA::Tensor &s_tensor) {
    json jobject = json::object();
    std::string precision_value = s_tensor.precision_as_string();
    if (!precision_value.empty()) {
        jobject.push_back(json::object_t::value_type("precision", precision_value));
    }
    std::string layout_value = s_tensor.layout_as_string();
    if (!layout_value.empty()) {
        jobject.push_back(json::object_t::value_type("layout", layout_value));
    }
    std::string name_value = s_tensor.name();
    if (!name_value.empty()) {
        jobject.push_back(json::object_t::value_type("name", name_value));
    }
    std::string model_name_value = s_tensor.model_name();
    if (!model_name_value.empty()) {
        jobject.push_back(json::object_t::value_type("model_name", model_name_value));
    }
    std::string layer_name_value = s_tensor.layer_name();
    if (!layer_name_value.empty()) {
        jobject.push_back(json::object_t::value_type("layer_name", layer_name_value));
    }
    std::string format_value = s_tensor.format();
    if (!format_value.empty()) {
        jobject.push_back(json::object_t::value_type("format", format_value));
    }
    if (!s_tensor.is_detection()) {
        std::string label_value = s_tensor.label();
        if (!label_value.empty()) {
            jobject.push_back(json::object_t::value_type("label", label_value));
        }
    }
    if (s_tensor.has_field("confidence")) {
        jobject.push_back(json::object_t::value_type("confidence", s_tensor.confidence()));
    }
    if (s_tensor.has_field("label_id")) {
        jobject.push_back(json::object_t::value_type("label_id", s_tensor.get_int("label_id")));
    }
    json data_array;
    if (s_tensor.precision() == GVA::Tensor::Precision::U8) {
        const std::vector<uint8_t> data = s_tensor.data<uint8_t>();
        for (guint i = 0; i < data.size(); i++) {
            data_array += data[i];
        }
    } else {
        const std::vector<float> data = s_tensor.data<float>();
        for (guint i = 0; i < data.size(); i++) {
            data_array += data[i];
        }
    }
    if (!data_array.is_null()) {
        jobject.push_back(json::object_t::value_type("data", data_array));
    }
    return jobject;
}

json convert_roi_detection(GstGvaMetaConvert *converter, GstBuffer *buffer) {
    json res;
    GVA::VideoFrame video_frame(buffer, converter->info);
    std::vector<GVA::RegionOfInterest> regions = video_frame.regions();
    for (std::vector<GVA::RegionOfInterest>::iterator it = regions.begin(); it != regions.end(); ++it) {
        gint id = 0;
        get_object_id(it->_meta(), &id);

        json jobject = json::object();

        if (converter->add_tensor_data) {
            jobject["tensors"] = json::array();
        }

        jobject.push_back({"x", it->_meta()->x});
        jobject.push_back({"y", it->_meta()->y});
        jobject.push_back({"w", it->_meta()->w});
        jobject.push_back({"h", it->_meta()->h});

        if (id != 0)
            jobject.push_back({"id", id});

        const gchar *roi_type = g_quark_to_string(it->_meta()->roi_type);

        if (roi_type) {
            jobject.push_back({"roi_type", roi_type});
        }
        for (GList *l = it->_meta()->params; l; l = g_list_next(l)) {

            GstStructure *s = (GstStructure *)l->data;
            const gchar *s_name = gst_structure_get_name(s);
            if (strcmp(s_name, "detection") == 0) {
                double xminval;
                double xmaxval;
                double yminval;
                double ymaxval;
                double confidence;
                int label_id;
                if (gst_structure_get(s, "x_min", G_TYPE_DOUBLE, &xminval, "x_max", G_TYPE_DOUBLE, &xmaxval, "y_min",
                                      G_TYPE_DOUBLE, &yminval, "y_max", G_TYPE_DOUBLE, &ymaxval, "confidence",
                                      G_TYPE_DOUBLE, &confidence, "label_id", G_TYPE_INT, &label_id, NULL)) {
                    json detection = {
                        {"bounding_box",
                         {{"x_min", xminval}, {"x_max", xmaxval}, {"y_min", yminval}, {"y_max", ymaxval}}},
                        {"confidence", confidence},
                        {"label_id", label_id}};

                    const gchar *label = g_quark_to_string(it->_meta()->roi_type);

                    if (label) {
                        detection.push_back({"label", label});
                    }
                    jobject.push_back(json::object_t::value_type("detection", detection));
                }
            } else {
                char *label;
                char *model_name;
                if (gst_structure_get(s, "label", G_TYPE_STRING, &label, "model_name", G_TYPE_STRING, &model_name,
                                      NULL)) {
                    const gchar *attribute_name = gst_structure_get_string(s, "attribute_name")
                                                      ? gst_structure_get_string(s, "attribute_name")
                                                      : s_name;
                    jobject.push_back(json::object_t::value_type(
                        attribute_name, {{"label", label}, {"model", {{"name", model_name}}}}));
                    g_free(label);
                    g_free(model_name);
                }
            }
            if (converter->add_tensor_data) {
                GVA::Tensor s_tensor = GVA::Tensor((GstStructure *)l->data);
                jobject["tensors"].push_back(convert_tensor(s_tensor));
            }
        }
        if (!jobject.empty()) {
            if (res["objects"].is_null()) {
                res["objects"] = json::array();
            }
            res["objects"].push_back(jobject);
        }
    }
    return res;
}

json convert_frame_tensors(GstGvaMetaConvert *converter, GstBuffer *buffer) {
    GVA::VideoFrame video_frame(buffer, converter->info);
    const std::vector<GVA::Tensor> tensors = video_frame.tensors();
    json array;
    for (std::vector<GVA::Tensor>::const_iterator it = tensors.begin(); it != tensors.end(); ++it) {
        array.push_back(convert_tensor(*it));
    }
    return array;
}

gboolean to_json(GstGvaMetaConvert *converter, GstBuffer *buffer) {
    GST_DEBUG_CATEGORY_INIT(gst_json_converter_debug, "jsonconverter", 0, "JSON converter");
    try {
        json jframe_tensors;
        json jframe = get_frame_data(converter, buffer);
        json jroi_detection = convert_roi_detection(converter, buffer);

        if (converter->add_tensor_data) {
            jframe_tensors = convert_frame_tensors(converter, buffer);
        }

        if (jroi_detection.empty() && jframe_tensors.empty()) {
            if (!converter->add_empty_detection_results) {
                GST_DEBUG_OBJECT(converter, "No detections found. Not posting JSON message");
                return TRUE;
            }
        }

        if (!jframe.is_null()) {
            if (!jroi_detection.empty()) {
                jframe.update(jroi_detection);
            }

            if (!jframe_tensors.empty()) {
                jframe["tensors"] = jframe_tensors;
            }
            std::string json_message = jframe.dump();
            GVA::VideoFrame video_frame(buffer, converter->info);
            video_frame.add_message(json_message);
            GST_INFO_OBJECT(converter, "JSON message: %s", json_message.c_str());
        }
    } catch (const std::exception &e) {
        GST_ERROR_OBJECT(converter, "%s", e.what());
        return FALSE;
    }
    return TRUE;
}
