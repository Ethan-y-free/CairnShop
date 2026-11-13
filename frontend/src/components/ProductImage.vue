<script setup lang="ts">
import { ref, watch } from 'vue';

const props = defineProps<{
    src: string;
    alt: string;
    label: string;
}>();

const failed = ref(false);

watch(
    () => props.src,
    () => {
        failed.value = false;
    }
);
</script>

<template>
    <img
        v-if="src && !failed"
        class="product-image"
        :src="src"
        :alt="alt"
        loading="lazy"
        @error="failed = true"
    />
    <span v-else class="product-image-fallback">
        {{ label.slice(0, 1) }}
    </span>
</template>
