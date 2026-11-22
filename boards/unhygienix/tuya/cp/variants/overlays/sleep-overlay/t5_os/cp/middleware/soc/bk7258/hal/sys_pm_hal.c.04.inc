/*----enter low voltage sleep-------*/
    unhygienix_native_sleep_stage(T5AISleepStage_CP_TransitionReady);
    unhygienix_native_sleep_wait();
    unhygienix_native_sleep_stage(T5AISleepStage_CP_TransitionReturned);