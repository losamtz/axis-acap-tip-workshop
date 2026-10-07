/* Minimal getter adapter for testing buffer reads without a camera or larod service. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define LAROD_TENSOR_DATA_TYPE_UINT8 1
#define LAROD_TENSOR_LAYOUT_NHWC 1
#define LAROD_FD_PROP_DMABUF 1

typedef struct {
    int fd;
    int64_t offset;
    size_t capacity;
    uint32_t properties;
    int datatype;
    int layout;
} larodTensor;
static inline int larodGetTensorDataType(larodTensor* tensor, void* error) {
    (void)error;
    return tensor->datatype;
}
static inline int larodGetTensorLayout(larodTensor* tensor, void* error) {
    (void)error;
    return tensor->layout;
}
static inline bool larodGetTensorFdProps(larodTensor* tensor, uint32_t* properties, void* error) {
    (void)error;
    *properties = tensor->properties;
    return true;
}
static inline bool larodGetTensorFdSize(larodTensor* tensor, size_t* capacity, void* error) {
    (void)error;
    *capacity = tensor->capacity;
    return true;
}
static inline int larodGetTensorFd(larodTensor* tensor, void* error) {
    (void)error;
    return tensor->fd;
}
static inline int64_t larodGetTensorFdOffset(larodTensor* tensor, void* error) {
    (void)error;
    return tensor->offset;
}
